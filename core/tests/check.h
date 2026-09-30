// A minimal test harness (no external dependency).
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_TESTS_CHECK_H_
#define DP3CORE_DYSCO_TESTS_CHECK_H_

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace dp3core::dysco::test {

struct TestCase {
  const char* name;
  std::function<void()> body;
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

inline int& FailureCount() {
  static int count = 0;
  return count;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> body) {
    Registry().push_back({name, std::move(body)});
  }
};

inline void ReportFailure(const char* file, int line, const std::string& what) {
  ++FailureCount();
  std::cerr << file << ':' << line << ": FAILED: " << what << '\n';
}

}  // namespace dp3core::dysco::test

#define DYSCO_CONCAT2(a, b) a##b
#define DYSCO_CONCAT(a, b) DYSCO_CONCAT2(a, b)

#define TEST_CASE(name)                                                  \
  static void name();                                                    \
  static ::dp3core::dysco::test::Registrar DYSCO_CONCAT(registrar_, name)( \
      #name, name);                                                      \
  static void name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) ::dp3core::dysco::test::ReportFailure(__FILE__, __LINE__, #cond); \
  } while (0)

#define CHECK_EQ(a, b)                                                     \
  do {                                                                     \
    const auto dysco_a_ = (a);                                             \
    const auto dysco_b_ = (b);                                             \
    if (!(dysco_a_ == dysco_b_)) {                                         \
      std::ostringstream dysco_s_;                                         \
      dysco_s_ << #a " == " #b " (" << dysco_a_ << " vs " << dysco_b_ << ")"; \
      ::dp3core::dysco::test::ReportFailure(__FILE__, __LINE__, dysco_s_.str()); \
    }                                                                      \
  } while (0)

// Checks that `expr` throws `type` and that what() contains `substring`.
#define CHECK_THROWS(expr, type, substring)                                  \
  do {                                                                       \
    bool dysco_thrown_ = false;                                              \
    try {                                                                    \
      (void)(expr);                                                          \
    } catch (const type& e) {                                                \
      dysco_thrown_ = true;                                                  \
      if (std::string(e.what()).find(substring) == std::string::npos)        \
        ::dp3core::dysco::test::ReportFailure(                               \
            __FILE__, __LINE__,                                              \
            std::string(#expr " threw, but message lacks '") + (substring) + \
                "': " + e.what());                                           \
    } catch (...) {                                                          \
      dysco_thrown_ = true;                                                  \
      ::dp3core::dysco::test::ReportFailure(__FILE__, __LINE__,              \
                                            #expr " threw the wrong type");  \
    }                                                                        \
    if (!dysco_thrown_)                                                      \
      ::dp3core::dysco::test::ReportFailure(__FILE__, __LINE__,              \
                                            #expr " did not throw");         \
  } while (0)

#endif
