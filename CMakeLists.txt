cmake_minimum_required (VERSION 3.12)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()
set(VCPKG_TARGET_TRIPLET "x64-windows")

project (
	MaxieStarfieldScripts
	VERSION 1.0.0
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there.")
endif()

add_executable (
	MaterialExtractor
	include/bs.h
	include/bsa.h
	include/cdb.h
	include/crc.h
	include/esp.h
	include/nif.h
	include/types.h
	include/util.h
	src/bsa.cpp
	src/crc.cpp
	src/jsontocdb.cpp
	src/nif.cpp
	src/util.cpp
)

target_include_directories(
	MaterialExtractor
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable (
	GetCrc
	include/crc.h
	include/util.h
	src/crc.cpp
	src/getcrc.cpp
	src/util.cpp
)

target_include_directories(
	GetCrc
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(nlohmann_json REQUIRED CONFIG)