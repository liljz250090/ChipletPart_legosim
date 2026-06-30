cmake_minimum_required(VERSION 3.14)
project(ChipletPart VERSION 1.0)

# Specify the C++ standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build type is Release.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING 
      "Choose the type of build, options are: Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()

# Set output directories.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Global compiler flags for better portability on your machine.
# ---------------------------------------------------------------------------
# Use a conservative optimization level and target native architecture.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=native")

# ---------------------------------------------------------------------------
# Boost
# ---------------------------------------------------------------------------
find_package(Boost 1.71.0 REQUIRED)
if(Boost_FOUND)
  message(STATUS "Boost found: enabling high-precision math functions")
  include_directories(${Boost_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "Boost library not found")
endif()

# ---------------------------------------------------------------------------
# OpenMP
# ---------------------------------------------------------------------------
option(ENABLE_OPENMP "Enable OpenMP for parallel processing" ON)
if(ENABLE_OPENMP)
  message(STATUS "OpenMP support is enabled")
  find_package(OpenMP)
  if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: enabling parallel processing")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    add_definitions(-DHAVE_OPENMP=1)
  else()
    message(STATUS "OpenMP not found: parallel processing will be disabled")
    add_definitions(-DHAVE_OPENMP=0)
  endif()
else()
  message(STATUS "OpenMP support is disabled by user configuration")
  add_definitions(-DHAVE_OPENMP=0)
endif()

# ---------------------------------------------------------------------------
# METIS and GKlib (local installation)
# ---------------------------------------------------------------------------
# Set the METIS root directory based on your local installation.
set(METIS_ROOT "${PROJECT_SOURCE_DIR}/METIS")
# Define GKlib root directory.
set(GKLIB_ROOT "${PROJECT_SOURCE_DIR}/GKlib")


# Include GKlib headers so that types like idx_t and real_t are defined.
include_directories(${GKLIB_ROOT})
include_directories(${GKLIB_ROOT}/include)

if(EXISTS "${METIS_ROOT}/include/metis.h")
    add_definitions(-DIDXTYPEWIDTH=64)
    add_definitions(-DREALTYPEWIDTH=64)
    message(STATUS "Using local METIS from ${METIS_ROOT}")
    include_directories(${METIS_ROOT}/include)
    
    # Build GKlib as a static library.
    file(GLOB GKLIB_SOURCES "${GKLIB_ROOT}/*.c" "${GKLIB_ROOT}/src/*.c")
    if(GKLIB_SOURCES)
        add_library(GKlib STATIC ${GKLIB_SOURCES})
        message(STATUS "Built GKlib static library from ${GKLIB_ROOT}")
    else()
        message(FATAL_ERROR "No GKlib source files found in ${GKLIB_ROOT}")
    endif()

    # If METIS has its own CMakeLists.txt, add it as a subdirectory.
    if(EXISTS "${METIS_ROOT}/CMakeLists.txt")
        file(MAKE_DIRECTORY "${METIS_ROOT}/build/xinclude")
        file(WRITE "${METIS_ROOT}/build/xinclude/metis.h"
            "#define IDXTYPEWIDTH 64\n#define REALTYPEWIDTH 64\n")
        file(READ "${METIS_ROOT}/include/metis.h" METIS_PUBLIC_HEADER)
        file(APPEND "${METIS_ROOT}/build/xinclude/metis.h" "${METIS_PUBLIC_HEADER}")
        file(COPY "${METIS_ROOT}/include/CMakeLists.txt"
             DESTINATION "${METIS_ROOT}/build/xinclude")
        add_subdirectory(${METIS_ROOT} ${METIS_ROOT}/build EXCLUDE_FROM_ALL)
        set(HAVE_METIS TRUE)
    else()
        # If METIS is already built and available as a library, specify its library directory.
        set(METIS_LIBRARY_DIR "${METIS_ROOT}/lib")
        link_directories(${METIS_LIBRARY_DIR})
        set(HAVE_METIS TRUE)
    endif()
else()
    message(FATAL_ERROR "Local METIS not found in ${METIS_ROOT}/include")
endif()

# ---------------------------------------------------------------------------
# Eigen
# ---------------------------------------------------------------------------
find_package(Eigen3 REQUIRED)
if(Eigen3_FOUND)    
    message(STATUS "Found installed Eigen library at ${Eigen3_INCLUDE_DIRS}")
    if(Eigen3_INCLUDE_DIRS)
        include_directories(${Eigen3_INCLUDE_DIRS})
    elseif(EXISTS "/usr/include/eigen3/Eigen/Dense")
        message(STATUS "Falling back to system Eigen headers at /usr/include/eigen3")
        include_directories("/usr/include/eigen3")
    endif()
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/eigen3/Eigen/Dense")
        message(STATUS "Found local Eigen library in include directory")
        include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include/eigen3")
    else()
        message(STATUS "Eigen not found - downloading from GitLab")
        include(FetchContent)
        FetchContent_Declare(
            eigen
            GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
            GIT_TAG 3.4.0
            GIT_SHALLOW ON
        )
        FetchContent_Populate(eigen)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/eigen3")
        file(COPY "${eigen_SOURCE_DIR}/Eigen" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/include/eigen3")
        file(COPY "${eigen_SOURCE_DIR}/unsupported" DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/include/eigen3")
        message(STATUS "Copied Eigen headers to ${CMAKE_CURRENT_SOURCE_DIR}/include/eigen3")
        include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include/eigen3")
    endif()
endif()

# ---------------------------------------------------------------------------
# yaml-cpp
# ---------------------------------------------------------------------------
find_package(yaml-cpp REQUIRED)

# ---------------------------------------------------------------------------
# Pugixml
# ---------------------------------------------------------------------------
add_library(pugixml STATIC ${PROJECT_SOURCE_DIR}/src/pugixml.cpp)
include_directories(${PROJECT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Global include directories
# ---------------------------------------------------------------------------
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/cost_model)
include_directories(${PROJECT_SOURCE_DIR}/cost_model/include)

# ---------------------------------------------------------------------------
# Options for building components
# ---------------------------------------------------------------------------
option(BUILD_COST_MODEL "Build the cost model component" ON)
option(BUILD_MAIN_APP "Build the main ChipletPart application" ON)
option(ENABLE_OPENDB_BACKEND "Build the OpenDB reader backend for ChipletPart" OFF)

if(ENABLE_OPENDB_BACKEND)
    list(APPEND CMAKE_PREFIX_PATH /home/liljz25/OpenROAD-flow-scripts/dependencies)
    find_package(spdlog CONFIG REQUIRED)
    find_package(absl CONFIG REQUIRED)
    include_directories(/home/liljz25/OpenROAD-flow-scripts/tools/OpenROAD/src/odb/include)
    include_directories(/home/liljz25/OpenROAD-flow-scripts/tools/OpenROAD/src/utl/include)
    if(EXISTS "/home/liljz25/OpenROAD-flow-scripts/tools/kepler-formal/thirdparty/naja/thirdparty/spdlog-1.17.0/include/spdlog/fmt/bundled/format.h")
        include_directories(/home/liljz25/OpenROAD-flow-scripts/tools/kepler-formal/thirdparty/naja/thirdparty/spdlog-1.17.0/include)
    elseif(EXISTS "/home/liljz25/anaconda3/envs/openroad-env/include/spdlog/details/os.h")
        include_directories(/home/liljz25/anaconda3/envs/openroad-env/include)
    elseif(EXISTS "/home/liljz25/anaconda3/pkgs/spdlog-1.17.0-h32bb505_0/include/spdlog/details/os.h")
        include_directories(/home/liljz25/anaconda3/pkgs/spdlog-1.17.0-h32bb505_0/include)
    endif()
endif()

if(BUILD_COST_MODEL)
    add_subdirectory(cost_model)
endif()

if(BUILD_MAIN_APP)
    set(CHIPLETPART_OPENDB_SOURCES)
    set(CHIPLETPART_OPENDB_HEADERS)
    if(ENABLE_OPENDB_BACKEND)
        list(APPEND CHIPLETPART_OPENDB_SOURCES
            src/ChipletPartOpenDBReader.cpp
            src/ChipletPartOpenDBWriter.cpp)
        list(APPEND CHIPLETPART_OPENDB_HEADERS
            src/ChipletPartOpenDBReader.h
            src/ChipletPartOpenDBWriter.h)
    endif()

    add_executable(chipletPart 
        src/Main.cpp 
        src/Hypergraph.cpp 
        src/Utilities.cpp 
        src/ChipletPart.cpp
        src/ChipletPart3DBloxReader.cpp
        src/FMRefiner.cpp 
        src/PriorityQueue.cpp 
        src/GeneticTechPartitioner.cpp
        src/floorplan.cpp
        src/KLRefiner.cpp
        src/KLRefiner.h
        src/Console.h
        src/ChipletPart.h
        src/ChipletPartIR.h
        src/ChipletPart3DBloxReader.h
        src/FMRefiner.h
        src/PriorityQueue.h
        src/Utilities.h
        src/Hypergraph.h
        src/floorplan.h
        src/GeneticTechPartitioner.h
        src/CanonicalGA.cpp
        src/CanonicalGA.h
        src/KLRefiner.cpp
        src/KLRefiner.h
        src/omp_utils.h
        ${CHIPLETPART_OPENDB_SOURCES}
        ${CHIPLETPART_OPENDB_HEADERS})
        
    target_link_libraries(chipletPart PRIVATE cost_model pugixml yaml-cpp ${Boost_LIBRARIES} Eigen3::Eigen)
    if(ENABLE_OPENDB_BACKEND)
        target_compile_definitions(chipletPart PRIVATE CHIPLETPART_ENABLE_OPENDB_BACKEND=1)
        target_link_libraries(chipletPart PRIVATE
            /home/liljz25/OpenROAD-flow-scripts/tools/OpenROAD/build/src/odb/src/db/libdb.a
            /home/liljz25/OpenROAD-flow-scripts/tools/OpenROAD/build/src/utl/libutl_lib.a
            spdlog::spdlog
            absl::synchronization)
    endif()
    
    # Link METIS and GKlib if available and not disabled (on non-Apple platforms)
    if(HAVE_METIS AND (NOT APPLE))
        target_link_libraries(chipletPart PRIVATE metis GKlib)
    endif()
    
    # Link OpenMP if found.
    if(OpenMP_CXX_FOUND)
        target_link_libraries(chipletPart PRIVATE ${OpenMP_CXX_LIBRARIES})
    endif()

    if(ENABLE_OPENDB_BACKEND)
        add_executable(annotateOdbWith3DBloxProperties
            src/AnnotateOdbWith3DBloxProperties.cpp)
        target_link_libraries(annotateOdbWith3DBloxProperties PRIVATE
            yaml-cpp
            /home/liljz25/OpenROAD-flow-scripts/tools/OpenROAD/build/src/odb/src/db/libdb.a
            /home/liljz25/OpenROAD-flow-scripts/tools/OpenROAD/build/src/utl/libutl_lib.a
            spdlog::spdlog
            absl::synchronization)
    endif()
endif()

# ---------------------------------------------------------------------------
# Compiler flags for different build types
# ---------------------------------------------------------------------------
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
