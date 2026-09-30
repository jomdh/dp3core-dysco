# SPDX-License-Identifier: GPL-3.0-only
add_library(dp3core_dysco_core
  src/bitpack.cc
  src/codec.cc
  src/dither.cc
  src/header.cc
  src/normalize.cc
  src/tables.cc
  src/version.cc
  src/weights.cc
)
add_library(dp3core_dysco::core ALIAS dp3core_dysco_core)
target_include_directories(dp3core_dysco_core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
set_target_properties(dp3core_dysco_core PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON)
dp3core_dysco_configure_target(dp3core_dysco_core)
find_package(Threads REQUIRED)
target_link_libraries(dp3core_dysco_core PUBLIC Threads::Threads)
target_compile_definitions(dp3core_dysco_core PRIVATE
  DP3CORE_DYSCO_VERSION_STRING="${PROJECT_VERSION}")
