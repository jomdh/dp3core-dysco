// SPDX-License-Identifier: GPL-3.0-only
#include "check.h"

int main() {
  using namespace dp3core::dysco::test;
  int failed_cases = 0;
  for (const TestCase& test_case : Registry()) {
    const int before = FailureCount();
    try {
      test_case.body();
    } catch (const std::exception& e) {
      ReportFailure(__FILE__, __LINE__,
                    std::string("uncaught exception: ") + e.what());
    }
    const bool ok = FailureCount() == before;
    if (!ok) ++failed_cases;
    std::cout << (ok ? "[ OK ] " : "[FAIL] ") << test_case.name << '\n';
  }
  std::cout << Registry().size() - static_cast<size_t>(failed_cases) << '/'
            << Registry().size() << " test cases passed\n";
  return failed_cases == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
