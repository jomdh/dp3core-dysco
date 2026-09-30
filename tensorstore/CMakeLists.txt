# TensorStore integration (design 7.3, option C: out-of-tree registration).
# Our own translation unit registers "dp3core.dysco" and
# "dp3core.dysco-weights" with TensorStore's zarr3 codec registry; no
# TensorStore source is patched. It uses TensorStore's internal zarr3 codec
# headers, so the TensorStore version is pinned.
# SPDX-License-Identifier: GPL-3.0-only
enable_language(C)
include(FetchContent)

set(DP3CORE_DYSCO_TENSORSTORE_URL
    "https://github.com/google/tensorstore/archive/refs/tags/v0.1.85.tar.gz"
    CACHE STRING "TensorStore source archive (pinned: v0.1.85)")
set(DP3CORE_DYSCO_TENSORSTORE_SHA256
    "be4cc2c130cb962b311aaf196678d30f35cd1bd73248b582c983978602cb2c5f"
    CACHE STRING "SHA-256 of the TensorStore archive")
option(DP3CORE_DYSCO_TENSORSTORE_TESTS "Build the TensorStore round-trip test" ON)

FetchContent_Declare(tensorstore
  URL "${DP3CORE_DYSCO_TENSORSTORE_URL}"
  URL_HASH SHA256=${DP3CORE_DYSCO_TENSORSTORE_SHA256})
FetchContent_MakeAvailable(tensorstore)

add_library(dp3core_dysco_tensorstore STATIC src/dysco_zarr3_codec.cc)
target_include_directories(dp3core_dysco_tensorstore PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(dp3core_dysco_tensorstore PUBLIC dp3core_dysco::core tensorstore::driver_zarr3)
target_compile_features(dp3core_dysco_tensorstore PUBLIC cxx_std_17)
set_target_properties(dp3core_dysco_tensorstore PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON POSITION_INDEPENDENT_CODE ON)
# Determinism flags only: TensorStore's headers are not -Wconversion clean.
target_compile_options(dp3core_dysco_tensorstore PRIVATE ${DP3CORE_DYSCO_DETERMINISM_FLAGS})

# Consumers link this: whole-archive, so the static registration object is
# kept even though nothing references it.
add_library(dp3core_dysco_tensorstore_whole INTERFACE)
target_link_libraries(dp3core_dysco_tensorstore_whole INTERFACE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,dp3core_dysco_tensorstore>")
add_library(dp3core_dysco::tensorstore ALIAS dp3core_dysco_tensorstore_whole)

if(DP3CORE_DYSCO_TENSORSTORE_TESTS)
  add_executable(test_tensorstore_codec tests/test_tensorstore_codec.cc
                 ${PROJECT_SOURCE_DIR}/core/tests/test_main.cc)
  target_include_directories(test_tensorstore_codec PRIVATE ${PROJECT_SOURCE_DIR}/core/tests)
  target_link_libraries(test_tensorstore_codec PRIVATE dp3core_dysco::tensorstore
                        tensorstore::tensorstore tensorstore::kvstore_file)
  add_test(NAME test_tensorstore_codec COMMAND test_tensorstore_codec)
endif()

add_executable(dp3core_dysco_ts_copy tools/ts_copy.cc)
target_link_libraries(dp3core_dysco_ts_copy PRIVATE dp3core_dysco::tensorstore
                      tensorstore::tensorstore tensorstore::kvstore_file)
