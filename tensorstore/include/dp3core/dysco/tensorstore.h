// TensorStore registration of the "dp3core.dysco" and "dp3core.dysco-weights"
// zarr3 codecs (design 7.3, option C).
// SPDX-License-Identifier: GPL-3.0-only
//
// Convention notice: unofficial DP3Core conventions, not endorsed by LOFAR,
// ASTRON or the MSv4 schema group.
#ifndef DP3CORE_DYSCO_TENSORSTORE_H_
#define DP3CORE_DYSCO_TENSORSTORE_H_

namespace dp3core::dysco::tensorstore {

/// The codecs register themselves from a static initializer when the
/// dp3core_dysco::tensorstore target is linked (it is linked whole-archive).
/// Calling this function is optional; it returns true if the registration
/// ran, and referencing it guarantees the object file is linked.
bool CodecsRegistered();

}  // namespace dp3core::dysco::tensorstore

#endif  // DP3CORE_DYSCO_TENSORSTORE_H_
