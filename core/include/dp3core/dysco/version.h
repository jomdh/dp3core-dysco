// SPDX-License-Identifier: GPL-3.0-only
//
// DP3Core-internal convention notice: the codec names, configuration fields and
// chunk layout of this library are unofficial DP3Core conventions, not endorsed
// by LOFAR, ASTRON or the MSv4 schema group.
#ifndef DP3CORE_DYSCO_VERSION_H_
#define DP3CORE_DYSCO_VERSION_H_

#include <string>

namespace dp3core::dysco {

/// Library version, e.g. "0.1.0".
std::string LibraryVersion();

/// Highest chunk format version this library reads and the one it writes.
inline constexpr unsigned kFormatVersion = 1;

}  // namespace dp3core::dysco

#endif
