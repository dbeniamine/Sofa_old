/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 beta 4      *
*                (c) 2006-2009 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Modules                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#define SOFA_IMAGE_MERGEDCATCHALLVECTOR_CPP

#include "mergedcatchallvector.h"
#include <sofa/core/ObjectFactory.h>

namespace sofa
{
namespace component
{
namespace engine
{

using namespace defaulttype;

SOFA_DECL_CLASS(MergedCatchAllVector)

int MergedCatchAllVectorClass = core::RegisterObject("MergedCatchAllVector")
        .add<MergedCatchAllVector<float > >(true)
        //.add<MergedCatchAllVector<unsigned float> >()
        .add<MergedCatchAllVector<short > >()
        .add<MergedCatchAllVector<unsigned short > >()
        .add<MergedCatchAllVector<int > >()
        .add<MergedCatchAllVector<unsigned int > >()
        .add<MergedCatchAllVector<double > >()
        //.add<MergedCatchAllVector<unsigned double> >()
        .add<MergedCatchAllVector<long > >()
        .add<MergedCatchAllVector<unsigned long > >()
        .add<MergedCatchAllVector<bool > >()
        .add<MergedCatchAllVector<sofa::defaulttype::Vec3f> >()
        .add<MergedCatchAllVector<sofa::defaulttype::Vec3d> >()
        ;

template class SOFA_IMAGE_API MergedCatchAllVector<float >;
//template class SOFA_IMAGE_API MergedCatchAllVector<unsigned float >;
template class SOFA_IMAGE_API MergedCatchAllVector<short >;
template class SOFA_IMAGE_API MergedCatchAllVector<unsigned short >;
template class SOFA_IMAGE_API MergedCatchAllVector<int >;
template class SOFA_IMAGE_API MergedCatchAllVector<unsigned int >;
template class SOFA_IMAGE_API MergedCatchAllVector<double >;
//template class SOFA_IMAGE_API MergedCatchAllVector<unsigned double >;
template class SOFA_IMAGE_API MergedCatchAllVector<long >;
template class SOFA_IMAGE_API MergedCatchAllVector<unsigned long >;
template class SOFA_IMAGE_API MergedCatchAllVector<bool >;
template class SOFA_IMAGE_API MergedCatchAllVector<sofa::defaulttype::Vec3f >;
template class SOFA_IMAGE_API MergedCatchAllVector<sofa::defaulttype::Vec3d >;




} //
} // namespace component
} // namespace sofa

