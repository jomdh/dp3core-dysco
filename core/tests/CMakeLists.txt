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
