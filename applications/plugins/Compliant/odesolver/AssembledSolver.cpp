#include "AssembledSolver.h"

#include <sofa/component/linearsolver/EigenSparseMatrix.h>
#include <sofa/component/linearsolver/EigenVector.h>
#include <sofa/core/ObjectFactory.h>
#include <sofa/simulation/common/MechanicalOperations.h>
#include <sofa/simulation/common/VectorOperations.h>

#include "assembly/AssemblyVisitor.h"
#include "constraint/ConstraintValue.h"

#include "utils/minres.h"
#include "utils/scoped.h"


using std::cerr;
using std::endl;

namespace sofa {
namespace component {
namespace odesolver {

SOFA_DECL_CLASS(AssembledSolver);
int AssembledSolverClass = core::RegisterObject("Example compliance solver using assembly").add< AssembledSolver >();

using namespace sofa::defaulttype;
using namespace sofa::helper;
using namespace core::behavior;




    propagate_visitor::propagate_visitor(const sofa::core::MechanicalParams* mparams)
        : simulation::MechanicalVisitor(mparams)
    {

    }

    propagate_visitor::Result propagate_visitor::fwdMappedMechanicalState(simulation::Node* node,
                                                                          core::behavior::BaseMechanicalState* state) {

        if( !node->forceField.empty() && node->forceField[0]->isCompliance.getValue() ) {
            // dont erase if compliance is present, cause set_state
            // already wrote data. compliance should be alone in the
            // node anyways
        } else {
            state->resetForce(mparams, out.getId(state));
        }

        // sadly this is not working :-/
//         state->vInit(mparams, out.getId(state), in.getId(state));

        return RESULT_CONTINUE;
    }

    propagate_visitor::Result propagate_visitor::fwdMechanicalState(simulation::Node* /*node*/,
                                                                    core::behavior::BaseMechanicalState* state) {
        state->resetForce(mparams, out.getId(state));
        // not working :-/
        // state->vInit(mparams, out.getId(state), in.getId(state));
        return RESULT_CONTINUE;
    }

    void propagate_visitor::bwdMechanicalMapping(simulation::Node* /*node*/, core::BaseMapping* map) {
        map->applyJT(mparams /* PARAMS FIRST */, out, out);
    }






    AssembledSolver::AssembledSolver()
        : warm_start(initData(&warm_start,
                              true,
                              "warm_start",
                              "warm start iterative solvers: avoids biasing solution towards zero (and speeds-up resolution)")),
          propagate_lambdas(initData(&propagate_lambdas,
                                     false,
                                     "propagate_lambdas",
                                     "propagate Lagrange multipliers in force vector at the end of time step")),
          stabilization(initData(&stabilization,
                                 false,
                                 "stabilization",
                                 "apply a stabilization pass on kinematic constraints requesting it")),
          debug(initData(&debug,
                         false,
                         "debug",
                         "print debug stuff")),
          alpha( initData(&alpha,
                          SReal(1),
                          "implicitVelocity",
                          "Weight of the next forces in the average forces used to update the velocities. 1 is implicit, 0 is explicit.")),
          beta( initData(&beta,
                         SReal(1),
                         "implicitPosition",
                         "Weight of the next velocities in the average velocities used to update the positions. 1 is implicit, 0 is explicit.")),

		stabilization_damping(initData(&stabilization_damping,
									   SReal(1e-7),
									   "stabilization_damping",
									   "stabilization damping hint to relax infeasible problems"))
    {
        storeDSol = false;
        assemblyVisitor = NULL;
    }




    void AssembledSolver::send(simulation::Visitor& vis) {
//        scoped::timer step("visitor execution");

        this->getContext()->executeVisitor( &vis );

    }


    void AssembledSolver::storeDynamicsSolution(bool b) { storeDSol = b; }


    void AssembledSolver::integrate( const core::MechanicalParams* params,
                                     core::MultiVecCoordId posId,
                                     core::MultiVecDerivId velId ) {
        scoped::timer step("position integration");
        SReal dt = params->dt();

        // integrate positions
        sofa::simulation::common::VectorOperations vop( params, this->getContext() );

        typedef core::behavior::BaseMechanicalState::VMultiOp VMultiOp;
        VMultiOp multi;

        multi.resize(1);

        multi[0].first = posId;
        multi[0].second.push_back( std::make_pair(posId, 1.0) );
        multi[0].second.push_back( std::make_pair(velId, beta.getValue() * dt) );

        vop.v_multiop( multi );
    }



    AssembledSolver::~AssembledSolver() {
        if( assemblyVisitor ) delete assemblyVisitor;
    }

    void AssembledSolver::cleanup() {
        sofa::simulation::common::VectorOperations vop( core::ExecParams::defaultInstance(), this->getContext() );
        vop.v_free( lagrange.id(), false, true );
        vop.v_free( _ck.id(), false, true );
    }



    // this is c_k computation (see compliant-reference.pdf, section 3)
    void AssembledSolver::compute_forces(const core::MechanicalParams& params,
                                         simulation::common::MechanicalOperations& mop,
                                         simulation::common::VectorOperations& vop,
                                         core::behavior::MultiVecDeriv& f,
                                         core::behavior::MultiVecDeriv& c )
    {
        scoped::timer step("implicit rhs computation");

#if 1 // NEW IMPLEMENTATION

        {
            scoped::timer substep("forces computation");

            simulation::MechanicalComputeStiffnessAndComplianceForcesVisitor fvis( &params, c, f ); // c = fk, f = fc
            send( fvis );
        }


        // f = fc + fk including mapped dofs
        {
            scoped::timer substep("force sum computation");

            simulation::MechanicalVOpVisitor eqvis( &params, f, f, c );
            eqvis.setMapped( true );
            send( eqvis );
        }

        scoped::timer substep("c_k computation");

        const SReal h = params.dt();

        {
            scoped::timer subsubstep("c_k=h.fk");

            // c_k = h fk
            c.teq( h );
        }

        {
            scoped::timer subsubstep("c_k+=MBKv");

            // M v_k
            const SReal mfactor = 1;

            // h (1-alpha) B v_k
            const SReal bfactor = h * (1 - alpha.getValue());

            // h^2 alpha (1 - beta ) K v_k
            const SReal kfactor = h * h * alpha.getValue() * (1 - beta.getValue());

            // note: K v_k factor only for stiffness dofs
            mop.addMBKv( c, mfactor, bfactor, kfactor );
        }


#else // OLD implementation, kept for a bit for comparison purpose


        MultiVecDeriv fk( &vop ); fk.realloc( &vop, false, true ); // only stiffness force (neglecting compliance)

        mop.computeForce(fk); // computing only stiffness force (neglecting compliance)

//        cerr<<SOFA_CLASS_METHOD<<"fk "<< fk << endl;

        // f = fk including mapped dofs
        {
            simulation::MechanicalVOpVisitor eqvis( &params, f, fk );
            eqvis.setMapped( true );
            send( eqvis );
        }

        SReal h = params.dt();

        // b = h fk
        c.eq( fk, h ); // copying only independant dofs

        // M v_k
        SReal mfactor = 1;

        // h (1-alpha) B v_k
        SReal bfactor = h * (1 - alpha.getValue());

        // h^2 alpha (1 - beta ) K v_k
        SReal kfactor = h * h * alpha.getValue() * (1 - beta.getValue());

        // note: K v_k factor only for stiffness dofs
        mop.addMBKv( c,
                     mfactor,
                     bfactor,
                     kfactor//,
//                     false // not necessary to clear mapped dofs, they are already null since only independant dofs have been copied
                     );

        vop.v_free( fk.id(), false, true );

        // computing fc (neglecting stiffness)
        MultiVecDeriv fc( &vop ); // only compliance force (neglecting stiffness)
        fc.realloc( &vop, false, true );
        {
            // adding compliance force to f
            simulation::MechanicalComputeComplianceForceVisitor mccfv( &params, fc );
            send( mccfv );
        }

//        cerr<<SOFA_CLASS_METHOD<<"fc "<< fc << endl;

        // f += fc including mapped dofs
        {
            simulation::MechanicalVOpVisitor peqvis(&params, f, f, fc, 1.0);
            peqvis.setMapped( true );
            send( peqvis );
        }

//        cerr<<SOFA_CLASS_METHOD<<"f "<< f << endl;

        vop.v_free( fc.id(), false, true );

#endif
    }



    void AssembledSolver::propagate(const core::MechanicalParams* params)
    {
        simulation::MechanicalPropagatePositionAndVelocityVisitor bob( params );
        send( bob );
    }



    void AssembledSolver::rhs_dynamics(vec& res, const system_type& sys, const vec& v, const MultiVecDeriv& b) const {
        assert( res.size() == sys.size() );

        unsigned off = 0;

        // master dofs: fetch what has been computed during compute_forces
        for(unsigned i = 0, end = sys.master.size(); i < end; ++i) {
            system_type::dofs_type* dofs = sys.master[i];

            unsigned dim = dofs->getMatrixSize();

            dofs->copyToBuffer(&res(off), b.id().getId(dofs), dim);

            off += dim;
        }


        // Applying the projection here (in flat vector representation) is great.
        // In compute_forces (in multivec representation) it would require a visitor (more expensive).
        res.head( sys.m ) = sys.P * res.head( sys.m );

        // compliant dofs
        for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
            system_type::dofs_type* dofs = sys.compliant[i];

            unsigned dim = dofs->getMatrixSize();

            // fetch constraint value if any
            BaseConstraintValue::SPtr value =
                    dofs->getContext()->get<BaseConstraintValue>( core::objectmodel::BaseContext::Local );

            // fallback TODO optimize ?
            if( !value ) {
                value = new ConstraintValue( dofs );
                dofs->getContext()->addObject( value );
                value->init();

            }

            value->dynamics(&res(off), dim, stabilization.getValue());
            off += dim;
        }
        assert( off == sys.size() );

        // adjust compliant value based on alpha/beta
        if( sys.n ) {

            if(alpha.getValue() != 1 ) res.tail( sys.n ) /= alpha.getValue();

            if( beta.getValue() != 1 ) {
                // TODO dofs->copyToBuffer(v_compliant, core::VecDerivId::vel(), dim); rather than sys.J * v, v_compliant is already mapped
                // TODO use v_compliant to implement constraint damping
                res.tail( sys.n ).noalias() = res.tail( sys.n ) - (1 - beta.getValue()) * (sys.J * v);
                res.tail( sys.n ) /= beta.getValue();
            }
        }
    }

    void AssembledSolver::rhs_correction(vec& res, const system_type& sys) const {
        assert( res.size() == sys.size() );

        // master dofs
        res.head( sys.m ).setZero();
        unsigned off = sys.m;

        // compliant dofs

        for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
            system_type::dofs_type* dofs = sys.compliant[i];

            unsigned dim = dofs->getMatrixSize();

            // fetch constraint value if any
            BaseConstraintValue::SPtr value =
                    dofs->getContext()->get<BaseConstraintValue>( core::objectmodel::BaseContext::Local );

            // fallback TODO optimize ?
            if(!value ) {
                value = new ConstraintValue( dofs );
                dofs->getContext()->addObject( value );
                value->init();
            }

            value->correction(&res(off), dim);

            off += dim;
        }

    }


    void AssembledSolver::buildMparams(core::MechanicalParams& mparams,
                                       const core::ExecParams& params,
                                       double dt) const
    {
        SReal mfactor = 1.0;
        SReal bfactor = -dt * alpha.getValue();
        SReal kfactor = -dt * dt * alpha.getValue() * beta.getValue();


        mparams.setExecParams( &params );
        mparams.setMFactor( mfactor );
        mparams.setBFactor( bfactor );
        mparams.setKFactor( kfactor );
        mparams.setDt( dt );

        mparams.setImplicitVelocity( alpha.getValue() );
        mparams.setImplicitPosition( beta.getValue() );
    }



    void AssembledSolver::get_state(vec& res, const system_type& sys, const core::MultiVecDerivId& multiVecId) const {

        assert( res.size() == sys.size() );

        unsigned off = 0;

        for(unsigned i = 0, end = sys.master.size(); i < end; ++i) {
            system_type::dofs_type* dofs = sys.master[i];

            unsigned dim = dofs->getMatrixSize();

            dofs->copyToBuffer(&res(off), multiVecId.getId(dofs), dim);
            off += dim;
        }

        //
        vec buffer;
        for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
            system_type::dofs_type* dofs = sys.compliant[i];

            unsigned dim = dofs->getMatrixSize();

            // resize as needed
            buffer.resize( std::max<unsigned>(buffer.size(), dim));
            dofs->copyToBuffer(buffer.data(), lagrange.id().getId( dofs ), dim);

            // momentum from force
            res.segment(off, dim) = buffer.head(dim) * sys.dt;

            off += dim;
        }

        assert( off == sys.size() );

    }


    void AssembledSolver::set_state(const system_type& sys, const vec& data, const core::MultiVecDerivId& multiVecId) const {

        assert( data.size() == sys.size() );

        unsigned off = 0;

        // TODO project v ?
        for(unsigned i = 0, end = sys.master.size(); i < end; ++i) {
            system_type::dofs_type* dofs = sys.master[i];

            unsigned dim = dofs->getMatrixSize();

            dofs->copyFromBuffer(multiVecId.getId(dofs), &data(off), dim);
            off += dim;
        }


        // we store constraint *force* (lambda / dt)
        vec buffer;

        const bool copy = propagate_lambdas.getValue();

        for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
            system_type::dofs_type* dofs = sys.compliant[i];

            unsigned dim = dofs->getMatrixSize();

            // resize as needed
            buffer.resize( std::max<unsigned>(buffer.size(), dim) );
            buffer.head(dim) = data.segment(off, dim) / sys.dt;

            dofs->copyFromBuffer(lagrange.id().getId( dofs ), buffer.data(), dim);

            if( copy ) dofs->copyFromBuffer(core::VecDerivId::force(), buffer.data(), dim);

            off += dim;
        }


    }




    void AssembledSolver::perform_assembly( const core::MechanicalParams *mparams, system_type& sys )
    {
        // max: il ya des auto_ptr pour ca.
        if( assemblyVisitor ) delete assemblyVisitor;
        assemblyVisitor = new simulation::AssemblyVisitor(mparams);

        // fetch nodes/data
        send( *assemblyVisitor );

        // assemble system
        sys = assemblyVisitor->assemble();
    }

    void AssembledSolver::solve(const core::ExecParams* params,
                                double dt,
                                core::MultiVecCoordId posId,
                                core::MultiVecDerivId velId) {
        assert(kkt);

        // mechanical parameters
        core::MechanicalParams mparams;
        this->buildMparams( mparams, *params, dt );
        mparams.setX(posId);
        mparams.setV(velId);

        simulation::common::MechanicalOperations mop( params, this->getContext() );
        simulation::common::VectorOperations vop( params, this->getContext() );
        MultiVecDeriv f( &vop, core::VecDerivId::force() ); // total force (stiffness + compliance) (f_k term)
        _ck.realloc( &vop, false, true ); // the right part of the implicit system (c_k term)

        // compute forces and implicit right part warning: must be
        // call before assemblyVisitor since the mapping's geometric
        // stiffness depends on its child force
        compute_forces( mparams, mop, vop, f, _ck );

        // assemble system
        perform_assembly( &mparams, sys );

        // debugging
        if( debug.getValue() ) sys.debug();

        // system factor
        {
            scoped::timer step("system factor");
            kkt->factor( sys );
        }

        if( sys.n )
        {
            scoped::timer step("lambdas alloc");
            lagrange.realloc( &vop, false, true );
        }

        // backup current state as correction might erase it, if any
        vec current( sys.size() );
        get_state( current, sys, velId );

        // system solution / rhs
        vec x(sys.size());
        vec rhs(sys.size());

        // ready to solve yo
        {
            scoped::timer step("system solve");

            // constraint stabilization
            if( sys.n && stabilization.getValue() ) {
                scoped::timer step("correction");

                x = vec::Zero( sys.size() );
                rhs_correction(rhs, sys);
				
                kkt->correct(x, sys, rhs, stabilization_damping.getValue() );
				
                if( debug.getValue() ) {
                    std::cerr << "correction rhs:" << std::endl
                              << rhs.transpose() << std::endl
                              << "solution:" << std::endl
                              << x.transpose() << std::endl;
                }

                set_state( sys, x, velId );
                integrate( &mparams, posId, velId );
            }

            // actual dynamics
            {
                scoped::timer step("dynamics");

                if( warm_start.getValue() ) x = current;
                else x = vec::Zero( sys.size() );

                rhs_dynamics(rhs, sys, current.head(sys.m), _ck );

                kkt->solve(x, sys, rhs);
				
                if( debug.getValue() ) {
                    std::cerr << "dynamics rhs:" << std::endl
                              << rhs.transpose() << std::endl
                              << "solution:" << std::endl
                              << x.transpose() << std::endl;
                }
                if( storeDSol ) {
                    dynamics_rhs = rhs;
                    dynamics_solution = x;
                }

                set_state( sys, x, velId );
                integrate( &mparams, posId, velId );

                // TODO is this even needed at this point ?
                propagate( &mparams );
            }

        }

        // propagate lambdas if asked to
        if( propagate_lambdas.getValue() && sys.n ) {
            scoped::timer step("lambda propagation");
            propagate_visitor prop( &mparams );

            prop.out = core::VecId::force();
            prop.in = lagrange.id();

            send( prop );
        }


    }




    void AssembledSolver::init() {

        // do want KKTSolver !
        kkt = this->getContext()->get<kkt_type>(core::objectmodel::BaseContext::Local);

        // TODO slightly less dramatic error, maybe ?
        if( !kkt ) throw std::logic_error("AssembledSolver needs a KKTSolver");
    }



}
}
}
