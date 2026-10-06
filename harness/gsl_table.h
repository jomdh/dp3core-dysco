// Quantization table built the way casacore Dysco builds it (GSL + long double).
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_HARNESS_GSL_TABLE_H_
#define DP3CORE_DYSCO_HARNESS_GSL_TABLE_H_

#include <vector>

#include "dp3core/dysco/types.h"

namespace dp3core::dysco::harness {

/// True when this binary was linked with GSL.
bool GslAvailable();

/// Decode levels, not including the NaN symbol. The construction follows
/// aroffringa/dysco stochasticencoder.cc (cumulative / invCumulative /
/// initializeTruncatedGaussian) and the unit-RMS scale in dyscodatacolumn.cc.
/// Throws std::runtime_error when GSL is not available.
std::vector<float> GslDecodeTable(unsigned bits, Distribution distribution, double truncation);

}  // namespace dp3core::dysco::harness

#endif
