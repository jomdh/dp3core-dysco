# Python extension module dp3core_dysco._core (design 7.2).
# SPDX-License-Identifier: GPL-3.0-only
find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)
pybind11_add_module(_core MODULE src/module.cc)
target_link_libraries(_core PRIVATE dp3core_dysco::core)
dp3core_dysco_configure_target(_core)
# pybind11 headers are not -Wconversion/-Wshadow clean.
target_compile_options(_core PRIVATE -Wno-conversion -Wno-shadow -Wno-sign-conversion)
install(TARGETS _core LIBRARY DESTINATION dp3core_dysco)
