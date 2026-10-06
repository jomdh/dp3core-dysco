# Casacore Dysco equivalence harness. Built only when DP3CORE_DYSCO_HARNESS=ON.
# SPDX-License-Identifier: GPL-3.0-only

find_path(CASACORE_INCLUDE_DIR casacore/tables/Tables/Table.h
  PATHS /opt/hilda/include /usr/include /usr/local/include)
find_library(CASACORE_TABLES_LIB casa_tables
  PATHS /opt/hilda/lib /opt/hilda/lib64 /usr/lib64 /usr/lib /usr/local/lib)
find_library(CASACORE_CASA_LIB casa_casa
  PATHS /opt/hilda/lib /opt/hilda/lib64 /usr/lib64 /usr/lib /usr/local/lib)
if(NOT CASACORE_INCLUDE_DIR OR NOT CASACORE_TABLES_LIB OR NOT CASACORE_CASA_LIB)
  message(FATAL_ERROR
    "DP3CORE_DYSCO_HARNESS=ON needs casacore (Table.h, libcasa_tables, libcasa_casa).")
endif()

find_package(GSL QUIET)

add_executable(dp3core_dysco_p2
  p2_harness.cc
  dysco_file.cc
  stats.cc
  gsl_table.cc)
target_include_directories(dp3core_dysco_p2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dp3core_dysco_p2 SYSTEM PRIVATE ${CASACORE_INCLUDE_DIR})
target_link_libraries(dp3core_dysco_p2 PRIVATE
  dp3core_dysco::core ${CASACORE_TABLES_LIB} ${CASACORE_CASA_LIB})
dp3core_dysco_configure_target(dp3core_dysco_p2)
if(GSL_FOUND)
  target_compile_definitions(dp3core_dysco_p2 PRIVATE DP3CORE_DYSCO_HAVE_GSL)
  target_link_libraries(dp3core_dysco_p2 PRIVATE GSL::gsl GSL::gslcblas)
endif()

add_executable(dp3core_dysco_bench bench_encode.cc)
target_link_libraries(dp3core_dysco_bench PRIVATE dp3core_dysco::core)
dp3core_dysco_configure_target(dp3core_dysco_bench)
