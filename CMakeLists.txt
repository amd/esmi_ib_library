#
# Minimum version of cmake required
#
cmake_minimum_required(VERSION 3.5.0)

## Set default module path if not already set
if(NOT DEFINED CMAKE_MODULE_PATH)
    set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
endif()
## Include common cmake modules
include(utils)

set(E_SMI "e_smi")
set(E_SMI_COMPONENT "lib${E_SMI}")
set(E_SMI_TARGET "${E_SMI}64")

# The following default version values should be updated as appropriate for
# ABI breaks (update MAJOR and MINOR), and ABI/API additions (update MINOR).
# Until ABI stabilizes VERSION_MAJOR will be 0. This should be over-ridden
# by git tags (through "git describe") when they are present.
set(PKG_VERSION_MAJOR 1)
set(PKG_VERSION_MINOR 0)
set(PKG_VERSION_PATCH 0)
set(PKG_VERSION_NUM_COMMIT 0)

################# Determine the library version #########################
## Setup the package version based on git tags.
set(PKG_VERSION_GIT_TAG_PREFIX "esmi_pkg_ver")
set(SO_VERSION_GIT_TAG_PREFIX "esmi_so_ver")

# provide git to utilities
find_program (GIT NAMES git)

get_package_version_number("1.0.0" ${PKG_VERSION_GIT_TAG_PREFIX} GIT)
# VERSION_* variables should be set by get_version_from_tag
message("Package version: ${PKG_VERSION_STR}")

# Debian package specific variables
# Set a default value for the package version
get_version_from_tag("1.0.0.0" ${SO_VERSION_GIT_TAG_PREFIX} GIT)

# VERSION_* variables should be set by get_version_from_tag
if ( ${E_PATCH_VERSION} )
    set ( VERSION_PATCH ${E_PATCH_VERSION})
    set(SO_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
else()
    set(SO_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}")
endif ()
set(${E_SMI}_VERSION_MAJOR "${VERSION_MAJOR}")
set(${E_SMI}_VERSION_MINOR "${VERSION_MINOR}")
set(${E_SMI}_VERSION_PATCH "0")
set(${E_SMI}_VERSION_BUILD "0")
message("SOVERSION: ${SO_VERSION_STRING}")

## Define default variable and variables for the optional build target
##  e_smi_lib-dev
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
	CACHE STRING "Location of E-SMI source code.")
set(CMAKE_INSTALL_PREFIX "/opt/esmi"
                                CACHE STRING "Default installation directory.")
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/esmi"
                                      CACHE STRING "Default packaging prefix.")

project(${E_SMI_TARGET})

# Create a configure file to get version info from within library
configure_file(
  "${PROJECT_SOURCE_DIR}/src/${E_SMI_TARGET}Config.in"
  "${PROJECT_SOURCE_DIR}/include/e_smi/${E_SMI_TARGET}Config.h")

if (NOT DEFINED CPACK_PACKAGE_VENDOR)
    set(CPACK_PACKAGE_VENDOR "AMD")
endif()

if (NOT DEFINED CPACK_PACKAGE_CONTACT)
    set(CPACK_PACKAGE_CONTACT "Advanced Micro Devices Inc.")
endif()

if (NOT DEFINED CPACK_PACKAGE_DESCRIPTION_SUMMARY)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
		"EPYC™ System Management Interface library")
endif()

if (NOT E_SMI_PACKAGE)
    set(E_SMI_PACKAGE e_smi_lib64)
endif()

set(CPACK_PACKAGE_FILE_NAME "${E_SMI_PACKAGE}-${PKG_VERSION_STR}")
## Verbose output.
set(CMAKE_VERBOSE_MAKEFILE on)

## Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fno-rtti -m64")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse -msse2 -std=c++11 ")
# Use this instead of above for 32 bit
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")

if ("${CMAKE_BUILD_TYPE}" STREQUAL Release)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -O0 -DDEBUG")
endif ()

set(SRC_DIR "src")
set(INC_DIR "include/e_smi")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SMI_SRC_LIST ${SMI_SRC_LIST} "${SRC_DIR}/e_smi.c")
set(SMI_SRC_LIST ${SMI_SRC_LIST} "${SRC_DIR}/e_smi_monitor.c")
set(SMI_SRC_LIST ${SMI_SRC_LIST} "${SRC_DIR}/e_smi_utils.c")

add_library(${E_SMI_TARGET} SHARED ${SMI_SRC_LIST} ${SMI_INC_LIST})
target_link_libraries(${E_SMI_TARGET} pthread rt m)

## Set the VERSION and SOVERSION values
set_property(TARGET ${E_SMI_TARGET} PROPERTY
                SOVERSION "${VERSION_MAJOR}")
set_property(TARGET ${E_SMI_TARGET} PROPERTY
                VERSION "${SO_VERSION_STRING}")

## If the library is a release, strip the target library
if ("${CMAKE_BUILD_TYPE}" STREQUAL Release)
    add_custom_command(
          TARGET ${E_SMI_TARGET}
          POST_BUILD COMMAND ${CMAKE_STRIP} lib${E_SMI_TARGET}.so)
endif ()

## Add the install directives for the runtime library.
install(TARGETS ${E_SMI_TARGET}
          LIBRARY DESTINATION ${E_SMI}/lib COMPONENT ${E_SMI_COMPONENT})
install(FILES ${SOURCE_DIR}/include/e_smi/e_smi.h
                                        DESTINATION e_smi/include/e_smi)

# Generate Doxygen documentation
find_package(Doxygen)
if (DOXYGEN_FOUND)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/esmi_doxygen.cfg
			${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(doc
	${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen" VERBATIM)
else()
  message("Doxygen is not found. Will not generate documents.")
endif(DOXYGEN_FOUND)
