#
# Minimum version of cmake required
#
cmake_minimum_required(VERSION 3.5.0)

#
#   cmake command line options:
#
#   1) Set ENABLE_STATIC_LIB to either "1" or "0".
#      If not set, the default value is "disbaled"
#
#      example #cmake -DENABLE_STATIC_LIB=1 ..

## Set default module path if not already set
if(NOT DEFINED CMAKE_MODULE_PATH)
    set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
endif()
## Include common cmake modules
include(utils)

set(ENABLE_STATIC_LIB OFF CACHE STRING
    "Whether to build as a Static Library. Set 1 to Enable, default is Disabled.")

set(E_SMI "e_smi")
set(E_SMI_COMPONENT "lib${E_SMI}")
set(E_SMI_TARGET "${E_SMI}64")
set(E_SMI_STATIC "${E_SMI_TARGET}_static")

if(${USE_CLANG})
	set(CMAKE_C_COMPILER "/usr/bin/clang")
	set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
	set(CMAKE_C_FLAGS "-O3 -march=native -flto")
endif()

################# Determine the library version #########################
## Setup the package version based on git tags.
set(PKG_VERSION_GIT_TAG_PREFIX "esmi_pkg_ver")

set(ENV{ESMI_BUILD_ID} "${ESMI_BUILD_ID}")

# provide git to utilities
find_program (GIT NAMES git)
get_package_version_number("1.0.0.0" ${PKG_VERSION_GIT_TAG_PREFIX} GIT)
message("Package version: ${PKG_VERSION_STR}")

# Debian package specific variables
# Set a default value for the package version

# VERSION_* variables should be set by get_package_version_number
if (${VERSION_PATCH})
    set (${E_SMI}_VERSION_PATCH "${VERSION_PATCH}")
else()
    set(${E_SMI}_VERSION_PATCH "0")
endif ()
set(SO_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set (${E_SMI}_VERSION_BUILD "${VERSION_ID}")
set(${E_SMI}_VERSION_MAJOR "${VERSION_MAJOR}")
set(${E_SMI}_VERSION_MINOR "${VERSION_MINOR}")
message("SOVERSION: ${SO_VERSION_STRING}")

set(DEB_INC "/usr/include/x86_64-linux-gnu/asm/amd_hsmp.h")
set(RHEL_INC "/usr/include/asm/amd_hsmp.h")
string(ASCII 27 Esc)
set(Magenta "${Esc}[35m")

## Define default variable and variables for the optional build target
##  e_smi_lib-dev
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
	CACHE STRING "Location of E-SMI source code.")
set(CMAKE_INSTALL_PREFIX "/opt/e-sms"
                                CACHE STRING "Default installation directory.")
set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/e-sms"
                                      CACHE STRING "Default packaging prefix.")

project(${E_SMI_TARGET})

set(E_SMI_HSMP_INC "${PROJECT_SOURCE_DIR}/include/asm/amd_hsmp.h")

if (NOT EXISTS ${E_SMI_HSMP_INC})
	if (NOT EXISTS ${DEB_INC} AND NOT EXISTS ${RHEL_INC})
		message(FATAL_ERROR "amd_hsmp.h is missing. Its needed for E-smi compilation.")
	else()
		message(${Magenta} "amd_hsmp.h is not available locally, its used from standard installation path.")
	endif()
endif()

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

include_directories([BEFORE] [SYSTEM] ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SMI_SRC_LIST ${SMI_SRC_LIST} "${SRC_DIR}/e_smi.c")
set(SMI_SRC_LIST ${SMI_SRC_LIST} "${SRC_DIR}/e_smi_monitor.c")
set(SMI_SRC_LIST ${SMI_SRC_LIST} "${SRC_DIR}/e_smi_utils.c")
set(SMI_SRC_LIST ${SMI_SRC_LIST} "${SRC_DIR}/e_smi_plat.c")

set(SMI_TOOL "e_smi_tool")

add_executable(${SMI_TOOL} "tools/e_smi_tool.c")

## If the tool to be linked with Static library
if ("${ENABLE_STATIC_LIB}" STREQUAL 1)
    target_link_libraries(${SMI_TOOL} ${E_SMI_STATIC})
else ()
    target_link_libraries(${SMI_TOOL} ${E_SMI_TARGET})
endif ()

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

## If the library to be build as a Static
if ("${ENABLE_STATIC_LIB}" STREQUAL 1)
    add_library(${E_SMI_STATIC} STATIC ${SMI_SRC_LIST} ${SMI_INC_LIST})
    target_link_libraries(${E_SMI_STATIC} pthread rt m)
    install(TARGETS ${E_SMI_STATIC}
          ARCHIVE DESTINATION ${E_SMI}/lib/static COMPONENT ${E_SMI_COMPONENT})
endif ()

## Add the install directives for the runtime library.
if (${CMAKE_INSTALL_PREFIX} STREQUAL /opt/e-sms)
install(TARGETS ${E_SMI_TARGET}
          LIBRARY DESTINATION ${E_SMI}/lib COMPONENT ${E_SMI_COMPONENT})
install(FILES ${SOURCE_DIR}/include/e_smi/e_smi.h
                                        DESTINATION e_smi/include/e_smi)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${SMI_TOOL}
					DESTINATION ${E_SMI}/bin)
else ()
install(TARGETS ${E_SMI_TARGET}
          LIBRARY DESTINATION lib COMPONENT ${E_SMI_COMPONENT})
install(FILES ${SOURCE_DIR}/include/e_smi/e_smi.h
                                        DESTINATION include/e_smi)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${SMI_TOOL}
					DESTINATION bin)
endif()

# Generate Doxygen documentation
find_package(Doxygen)
if (DOXYGEN_FOUND)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/esmi_doxygen.cfg
			${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/latex/refman.tex
       COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
       DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/docs/esmi_doxygen.cfg
       WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/latex/refman.pdf
       COMMAND make  > /dev/null
       COMMAND cp  ${CMAKE_CURRENT_BINARY_DIR}/latex/refman.pdf
              ${CMAKE_CURRENT_SOURCE_DIR}/ESMI_Manual.pdf
       DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/latex/refman.tex
       WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/latex)

  # To create directory "rel" to contain seprate latex, html, etc.
  set(folder "rel")
  add_custom_command(OUTPUT ${folder}
	COMMAND mkdir ${folder}
  )
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/release_notes_doxygen.cfg
                        ${CMAKE_CURRENT_BINARY_DIR}/rel/Doxyfile @ONLY)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rel/latex/refman.tex
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/rel/Doxyfile
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/docs/release_notes_doxygen.cfg
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rel)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/rel/latex/refman.pdf
        COMMAND make  > /dev/null
        COMMAND cp  ${CMAKE_CURRENT_BINARY_DIR}/rel/latex/refman.pdf
                ${CMAKE_CURRENT_SOURCE_DIR}/ESMI_IB_Release_Notes.pdf
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/rel/latex/refman.tex
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rel/latex)

  add_custom_target(doc DEPENDS ${folder} ${CMAKE_CURRENT_BINARY_DIR}/latex/refman.pdf
			${CMAKE_CURRENT_BINARY_DIR}/rel/latex/refman.pdf)

else()
  message("Doxygen is not found. Will not generate documents.")
endif(DOXYGEN_FOUND)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ESMI_Manual.pdf
			DESTINATION ${E_SMI}/doc/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/README.md
			DESTINATION ${E_SMI}/doc/)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ESMI_IB_Release_Notes.pdf
			DESTINATION ${E_SMI}/doc/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/docs/RELEASENOTES.md
			DESTINATION ${E_SMI}/doc/)

# General package specific variables
set(CPACK_GENERATOR "DEB;RPM"  CACHE STRING "Default packaging generators.")
set(CPACK_PACKAGE_NAME ${E_SMI_PACKAGE})
set(CPACK_PACKAGE_VERSION ${PKG_VERSION_STR})
set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_SOURCE_DIR}/package.txt)

# DEB package specific variables
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/DEBIAN/postinst;${CMAKE_CURRENT_SOURCE_DIR}/DEBIAN/prerm")
set(CPACK_DEBIAN_PACKAGE_NAME  ${E_SMI_PACKAGE})
set(CPACK_DEBIAN_PACKAGE_VERSION ${PKG_VERSION_STR})

# RPM package specific variables
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
set(CPACK_RPM_PACKAGE_AUTOREQ "no")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE
       "${CMAKE_CURRENT_SOURCE_DIR}/RPM/rpm_post")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE
       "${CMAKE_CURRENT_SOURCE_DIR}/RPM/rpm_postun")
set(CPACK_RPM_PACKAGE_NAME  ${E_SMI_PACKAGE})
set(CPACK_RPM_PACKAGE_VERSION ${PKG_VERSION_STR})

include (CPack)
