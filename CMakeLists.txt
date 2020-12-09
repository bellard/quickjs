cmake_minimum_required(VERSION 3.5)
project(quickjs)

# Define the target
add_library(quickjs STATIC
	cutils.c
	libbf.c
	libregexp.c
	libunicode.c
	qjs.c
	qjsc.c
	quickjs.c
	quickjs-libc.c
	unicode_gen.c
	)

add_library(quickjs::quickjs ALIAS quickjs)

# Read the version off the VERSION file
file(READ VERSION CONFIG_VERSION)
string(REGEX REPLACE "\n$" "" CONFIG_VERSION "${CONFIG_VERSION}")

set_target_properties(quickjs PROPERTIES
	VERSION ${CONFIG_VERSION}
	)

# Add #defines and dependencies
target_compile_definitions(quickjs PRIVATE
	CONFIG_VERSION="${CONFIG_VERSION}"
	CONFIG_BIGNUM=1
	)

if (UNIX)
	target_link_libraries(quickjs PRIVATE m dl pthread)
	target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
endif()

# Install the target
include(GNUInstallDirs)
install(TARGETS quickjs EXPORT quickjsTargets
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/quickjs COMPONENT quickjs
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/quickjs COMPONENT quickjs
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT quickjs
	)

# Create and install CMake configs
include(CMakePackageConfigHelpers)

write_basic_package_version_file(quickjsConfigVersion.cmake
	VERSION ${CONFIG_VERSION}
	COMPATIBILITY SameMajorVersion
	)
configure_package_config_file(quickjsConfig.cmake.in
	quickjsConfig.cmake
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/quickjs
	)
install(EXPORT quickjsTargets
	FILE quickjsTargets.cmake
	NAMESPACE quickjs::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/quickjs
	COMPONENT quickjs
	)
install(FILES
	"${CMAKE_CURRENT_BINARY_DIR}/quickjsConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/quickjsConfigVersion.cmake"
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/quickjs
	COMPONENT quickjs
	)

# Install includes
install(FILES
	quickjs.h quickjs-libc.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quickjs
	COMPONENT quickjs
	)
target_include_directories(quickjs PUBLIC
	$<BUILD_INTERFACE:${quickjs_BINARY_DIR}>
	$<BUILD_INTERFACE:${quickjs_SOURCE_DIR}>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	)
