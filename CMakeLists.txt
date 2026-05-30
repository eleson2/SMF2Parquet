cmake_minimum_required(VERSION 3.28)
project(smf2parquet CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standalone build: the mf:: reader primitives and smf:: parse cores are vendored
# under vendor/mf_records/ (a copy of the header-only MF-records-to-C library — see
# vendor/mf_records/SYNC.md). SMF2Parquet has NO dependency on any sibling project.

add_executable(smf2parquet src/main.cpp)
target_include_directories(smf2parquet PRIVATE src vendor/mf_records)

# Self-contained SMF test-data generator (no external deps; builds in any config).
add_executable(gen_test_smf tests/gen_test_smf.cpp)

# --- Parquet sink layer ---------------------------------------------------------
# Requires Apache Arrow + Parquet C++ (libarrow-dev / libparquet-dev). Without it the
# tool still builds and runs (parsing + per-type record counting). Enable with:
#   cmake -B build -DSMF2PARQUET_WITH_PARQUET=ON
option(SMF2PARQUET_WITH_PARQUET "Build the Parquet sink layer (requires Arrow/Parquet)" OFF)
if(SMF2PARQUET_WITH_PARQUET)
    find_package(Arrow   CONFIG REQUIRED)
    find_package(Parquet CONFIG REQUIRED)
    target_link_libraries(smf2parquet PRIVATE Arrow::arrow_shared Parquet::parquet_shared)
    target_compile_definitions(smf2parquet PRIVATE SMF2PARQUET_WITH_PARQUET=1)

    # Read-back / verification helper for the emitted Parquet files.
    add_executable(pq_dump tools/pq_dump.cpp)
    target_link_libraries(pq_dump PRIVATE Arrow::arrow_shared Parquet::parquet_shared)
endif()

# --- Pulling a dependency straight from git (optional; nothing below is active) ---
# CMake's FetchContent is the closest analogue to a package.json dependency: it
# declares a library by git URL + a pinned tag/commit and builds it from source.
# This project does not use it yet (Arrow/Parquet come from the system via
# find_package; the mf:: / smf:: cores are vendored under vendor/mf_records/).
# See docs/cmake-dependencies.md for the full how-to and the find_package vs
# FetchContent vs vendor decision guide. To add one, uncomment and adapt:
#
#   include(FetchContent)
#   FetchContent_Declare(
#       fmt
#       GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#       GIT_TAG        11.0.2          # PIN to a release tag or commit SHA (the lockfile)
#       GIT_SHALLOW    TRUE
#       FIND_PACKAGE_ARGS              # reuse an installed copy if present (CMake >= 3.24)
#   )
#   FetchContent_MakeAvailable(fmt)
#   target_link_libraries(smf2parquet PRIVATE fmt::fmt)
