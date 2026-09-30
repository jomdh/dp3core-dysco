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

if(DP3CORE_DYSCO_GSL_CROSSCHECK)
  find_package(GSL QUIET)
  if(GSL_FOUND)
    dp3core_dysco_add_test(test_tables_gsl)
    target_link_libraries(test_tables_gsl PRIVATE GSL::gsl)
  else()
    message(STATUS "GSL not found: skipping test_tables_gsl")
  endif()
endif()
