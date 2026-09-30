# SPDX-License-Identifier: GPL-3.0-only

# One executable per test file; each is a CTest test.
function(dp3core_dysco_add_test name)
  add_executable(${name} ${name}.cc test_main.cc ${ARGN})
  target_link_libraries(${name} PRIVATE dp3core_dysco::core)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  dp3core_dysco_configure_target(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

dp3core_dysco_add_test(test_version)
dp3core_dysco_add_test(test_tables)
dp3core_dysco_add_test(test_header)
dp3core_dysco_add_test(test_bitpack)
dp3core_dysco_add_test(test_dither)

if(DP3CORE_DYSCO_GSL_CROSSCHECK)
  find_package(GSL QUIET)
  if(GSL_FOUND)
    dp3core_dysco_add_test(test_tables_gsl)
    target_link_libraries(test_tables_gsl PRIVATE GSL::gsl)
  else()
    message(STATUS "GSL not found: skipping test_tables_gsl")
  endif()
endif()

# Comparison with the original Dysco sources (not part of this repository).
set(DP3CORE_DYSCO_REFERENCE_DIR "" CACHE PATH
    "Path to a checkout of aroffringa/dysco; enables test_reference_dysco")
if(DP3CORE_DYSCO_REFERENCE_DIR)
  if(NOT EXISTS "${DP3CORE_DYSCO_REFERENCE_DIR}/bytepacker.h")
    message(FATAL_ERROR "DP3CORE_DYSCO_REFERENCE_DIR does not look like a Dysco checkout")
  endif()
  find_package(GSL REQUIRED)
  # Dysco's own casacore-free sources, compiled unmodified (warnings off).
  add_library(dysco_reference STATIC
    ${DP3CORE_DYSCO_REFERENCE_DIR}/stochasticencoder.cc)
  target_include_directories(dysco_reference SYSTEM PUBLIC ${DP3CORE_DYSCO_REFERENCE_DIR})
  target_compile_options(dysco_reference PRIVATE ${DP3CORE_DYSCO_DETERMINISM_FLAGS} -w)
  target_link_libraries(dysco_reference PUBLIC GSL::gsl)
  dp3core_dysco_add_test(test_reference_dysco)
  target_link_libraries(test_reference_dysco PRIVATE dysco_reference)
  # Dysco's Dictionary::symbol() asserts iter > begin(), which fires for
  # values at or below the lowest level (symbol 0) in Debug builds; its
  # release behaviour (symbol 0) is what the format specifies.
  target_compile_definitions(dysco_reference PRIVATE NDEBUG)
  target_compile_definitions(test_reference_dysco PRIVATE NDEBUG)
endif()
