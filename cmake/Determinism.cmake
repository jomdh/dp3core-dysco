# Compiler settings that keep the codec's output bit-reproducible
# (spec/SPEC.md, "Determinism rules").
# SPDX-License-Identifier: GPL-3.0-only

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$" OR
   (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 4))
  message(FATAL_ERROR "32-bit x86 (x87 floating point) is not supported: results would not be reproducible.")
endif()

set(DP3CORE_DYSCO_DETERMINISM_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # No fused multiply-add contraction, no value-changing optimisations.
  list(APPEND DP3CORE_DYSCO_DETERMINISM_FLAGS -ffp-contract=off -fno-fast-math)
elseif(MSVC)
  list(APPEND DP3CORE_DYSCO_DETERMINISM_FLAGS /fp:precise)
endif()

set(DP3CORE_DYSCO_WARNING_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  list(APPEND DP3CORE_DYSCO_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(DP3CORE_DYSCO_WERROR)
    list(APPEND DP3CORE_DYSCO_WARNING_FLAGS -Werror)
  endif()
endif()

# Applies the flags to a target.
function(dp3core_dysco_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_options(${target} PRIVATE ${DP3CORE_DYSCO_DETERMINISM_FLAGS} ${DP3CORE_DYSCO_WARNING_FLAGS})
endfunction()
