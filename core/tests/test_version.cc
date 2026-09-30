// SPDX-License-Identifier: GPL-3.0-only
#include "check.h"
#include "dp3core/dysco/version.h"

TEST_CASE(VersionIsSet) {
  CHECK_EQ(dp3core::dysco::LibraryVersion(), std::string("0.1.0"));
  CHECK_EQ(dp3core::dysco::kFormatVersion, 1u);
}
