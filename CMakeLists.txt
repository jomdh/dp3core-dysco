# dp3core-dysco-zarr: a standalone Dysco codec for Zarr.
# SPDX-License-Identifier: GPL-3.0-only
#
# Convention notice: codec names, configuration fields and the chunk layout are
# unofficial DP3Core conventions, not endorsed by LOFAR, ASTRON or the MSv4 group.

cmake_minimum_required(VERSION 3.20)
project(dp3core_dysco VERSION 0.1.0 LANGUAGES CXX)

option(DP3CORE_DYSCO_TENSORSTORE "Build the TensorStore integration (needs CMake >= 3.24)" OFF)
option(DP3CORE_DYSCO_WERROR "Treat compiler warnings as errors" OFF)
option(DP3CORE_DYSCO_GSL_CROSSCHECK "Compare the portable tables with GSL-built tables in the tests (if GSL is found)" ON)
option(BUILD_TESTING "Build the tests" ON)
option(DP3CORE_DYSCO_PYTHON "Build the Python extension (normally via pip / scikit-build-core)" OFF)
option(DP3CORE_DYSCO_HARNESS "Build the casacore Dysco equivalence harness (needs casacore)" OFF)

if(DP3CORE_DYSCO_TENSORSTORE)
  # D9 / tensorstore-build-cost.md: TensorStore needs CMake >= 3.24.
  if(CMAKE_VERSION VERSION_LESS 3.24)
    message(FATAL_ERROR "DP3CORE_DYSCO_TENSORSTORE=ON needs CMake >= 3.24 (found ${CMAKE_VERSION}).")
  endif()
endif()

include(cmake/Determinism.cmake)
enable_testing()

add_subdirectory(core)

if(DP3CORE_DYSCO_PYTHON)
  add_subdirectory(python)
endif()

if(DP3CORE_DYSCO_TENSORSTORE)
  add_subdirectory(tensorstore)
endif()

if(BUILD_TESTING)
  add_subdirectory(core/tests)
endif()

if(DP3CORE_DYSCO_HARNESS)
  add_subdirectory(harness)
endif()
