include(${SOFA_CMAKE_DIR}/preProject.cmake)

set(HEADER_FILES

	../BaseGUI.h
        ../BaseViewer.h
	../BatchGUI.h
	../ColourPickingVisitor.h
	../MouseOperations.h
	../OperationFactory.h
	../PickHandler.h
	../FilesRecentlyOpenedManager.h
	../SofaGUI.h
	../ViewerFactory.h
	../GUIManager.h
	)

set(SOURCE_FILES

	../BaseGUI.cpp
        ../BaseViewer.cpp
	../BatchGUI.cpp
	../ColourPickingVisitor.cpp
	../FilesRecentlyOpenedManager.cpp
	../MouseOperations.cpp
	../PickHandler.cpp
	../GUIManager.cpp
	../ViewerFactory.cpp
	)
	
set(GROUP_BASE_DIR "..")

add_library(${PROJECT_NAME} SHARED ${HEADER_FILES} ${SOURCE_FILES})


AddCompilerDefinitions("SOFA_BUILD_SOFAGUI")
AddLinkerDependencies(SofaComponentMain SofaGraphComponent SofaBaseCollision SofaUserInteraction SofaBaseVisual)
if(SOFA-MISC_USE_LIKWID)
    AddLinkerDependencies(${LIKWID_LIBRARIES})
endif()
include(${SOFA_CMAKE_DIR}/postProject.cmake)

