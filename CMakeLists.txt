cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

################### Variables. ####################
# Change if you want modify path or other values. #
###################################################

set(PROJECT_NAME PythonEmbedding)

############## CMake Project ################
#        The main options of project        #
#############################################

project(${PROJECT_NAME} CXX)
# CXX version
set(CMAKE_CXX_STANDARD 11)


# Define Python libraries
set(PythonLibs_VERSION 3.7 CACHE STRING "Python version used to build VTK and Boost")

find_package(PythonLibs ${PythonLibs_VERSION} EXACT REQUIRED)

string(REPLACE "." "" Boost_Python_VERSION ${PythonLibs_VERSION})


# Define VTK directory
find_package(VTK REQUIRED)
include(${VTK_USE_FILE})


# Add vtk headers and lib directory and change their backslash to forward slash in Windows
#include_directories(${VTK_INCLUDE_DIR})
#link_directories(${VTK_LIB_DIR})

if(MSVC AND VTK_DIR)
    string(REPLACE "\\" "/" VTK_DIR ${VTK_DIR})
endif()


################# Flags ################
# Defines Flags for Windows and Linux. #
########################################

if(NOT MSVC)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
   if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
   endif()
endif(NOT MSVC)

################ Files ################
#   --   Add files to project.   --   #
#######################################

file(GLOB SRC_FILES "*.h" "*.cpp")
file(GLOB PY_FILES "*.py")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Generate libraries for copying
list(GET PYTHON_LIBRARIES 1 PYTHON_LIB_0)
get_filename_component(PYTHON_LIBRARY_DIR_RELEASE ${PYTHON_LIB_0} DIRECTORY)
message(STATUS "Python Library path ${PYTHON_LIBRARY_DIR_RELEASE} from ${PYTHON_LIB_0}")
foreach(PYTHON_FILE ${PYTHON_LIBRARIES})
    if(EXISTS ${PYTHON_FILE})
        get_filename_component(TMP_COMPONENT ${PYTHON_FILE} NAME)
        list(APPEND PYTHON_LIBRARY_NAMES ${TMP_COMPONENT})
    endif()
endforeach()
message(STATUS "Python library names: ${PYTHON_LIBRARY_NAMES}")

message (STATUS "VTK_VERSION: ${VTK_VERSION}")

file(COPY "${CMAKE_SOURCE_DIR}\\setup.bat" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY ${PY_FILES} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

if (VTK_VERSION VERSION_LESS "8.90.0")
  # old system
  include_directories(${PYTHON_INCLUDE_DIRS}) 
  include(${VTK_USE_FILE})
  add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SRC_FILES})
  target_link_libraries(${PROJECT_NAME} PRIVATE ${PYTHON_LIBRARIES} ${VTK_LIBRARIES})
else ()
  include_directories(${PYTHON_INCLUDE_DIRS})
  # include all components
  add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SRC_FILES})
  target_link_libraries(${PROJECT_NAME} PRIVATE ${PYTHON_LIBRARIES} ${VTK_LIBRARIES})
  # vtk_module_autoinit is needed
  vtk_module_autoinit(
    TARGETS ${PROJECT_NAME}
    MODULES ${VTK_LIBRARIES}
  )	
endif ()