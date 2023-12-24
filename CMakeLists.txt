cmake_minimum_required (VERSION 3.12)

project (
	MaxieStarfieldScripts
	VERSION 1.0.0
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there.")
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)
add_subdirectory(bsa)
add_subdirectory(nifly)

# --- Material Extractor ---
add_executable (
	MaterialExtractor
	include/bs.h
	include/bsa.h
	include/cdb.h
	include/crc.h
	include/esp.h
	include/nif.h
	include/paths.h
	include/types.h
	include/util.h
	src/bsa.cpp
	src/crc.cpp
	src/cdbtojson.cpp
	src/nif.cpp
	src/paths.cpp
	src/util.cpp
)

target_include_directories(
	MaterialExtractor
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	MaterialExtractor
	PRIVATE
	bsa
	nifly
	nlohmann_json::nlohmann_json
)

# --- Get Crc ---
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

# --- Dump Db ---
add_executable (
	DumpDb
	include/bs.h
	include/bsa.h
	include/cdb.h
	include/crc.h
	include/esp.h
	include/nif.h
	include/paths.h
	include/types.h
	include/util.h
	src/bsa.cpp
	src/crc.cpp
	src/DumpDb.cpp
	src/nif.cpp
	src/paths.cpp
	src/util.cpp
)

target_include_directories(
	DumpDb
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	DumpDb
	PRIVATE
	bsa
	nifly
	nlohmann_json::nlohmann_json
)

# --- Extend Nif ---
add_executable(
	ExtendNif
	include/util.h
	src/extendnif.cpp
	src/util.cpp
)

target_include_directories(
	ExtendNif
	PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	ExtendNif
	PRIVATE
	nifly
	nlohmann_json::nlohmann_json
)

# --- Cdb Library ---
add_library(
        CdbLib
        STATIC
        include/bs.h
        include/bsa.h
        include/cdb.h
        include/crc.h
        include/esp.h
        include/nif.h
        include/paths.h
        include/types.h
        include/util.h
        include/mat.h
        src/bsa.cpp
        src/crc.cpp
        src/mat.cpp
        src/nif.cpp
        src/paths.cpp
        src/util.cpp
)

target_include_directories(
	CdbLib
	PUBLIC
	"$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
	"$<INSTALL_INTERFACE:include>"
	#${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	CdbLib
	PUBLIC
	bsa
	nifly
	nlohmann_json::nlohmann_json
        miniz::miniz
)