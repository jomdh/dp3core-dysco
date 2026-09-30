// Golden XXH3-64 checksums of the decode tables (spec/SPEC.md section 4.4).
// SPDX-License-Identifier: GPL-3.0-only
#ifndef DP3CORE_DYSCO_TESTS_GOLDEN_TABLES_H_
#define DP3CORE_DYSCO_TESTS_GOLDEN_TABLES_H_

#include <cstdint>

#include "dp3core/dysco/types.h"

struct GoldenTable {
  unsigned bits;
  dp3core::dysco::Distribution distribution;
  double truncation;
  std::uint64_t checksum;
};

inline constexpr GoldenTable kGoldenTables[] = {
    {2, dp3core::dysco::Distribution::kGaussian, 0.0, 0x0757B3C32D20B6E2ull},
    {2, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0x69FD273DE1F32A98ull},
    {2, dp3core::dysco::Distribution::kUniform, 0.0, 0x161644C60B116115ull},
    {3, dp3core::dysco::Distribution::kGaussian, 0.0, 0x1F2CB4F6A61638E1ull},
    {3, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0x4B524A0C7C7BD359ull},
    {3, dp3core::dysco::Distribution::kUniform, 0.0, 0xAC80EBA4797D190Aull},
    {4, dp3core::dysco::Distribution::kGaussian, 0.0, 0xFBB3278EF57E78AEull},
    {4, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0xB5CE78A26D4BC204ull},
    {4, dp3core::dysco::Distribution::kUniform, 0.0, 0x87B4E5827A83B93Cull},
    {6, dp3core::dysco::Distribution::kGaussian, 0.0, 0x80223051687BA21Eull},
    {6, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0x39C6F3EF2656AD92ull},
    {6, dp3core::dysco::Distribution::kUniform, 0.0, 0xEA97EA1BBA49662Full},
    {8, dp3core::dysco::Distribution::kGaussian, 0.0, 0x8BB24822AC51CDD8ull},
    {8, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0xF573D8F73FC3C778ull},
    {8, dp3core::dysco::Distribution::kUniform, 0.0, 0xA3C17EAC4A8B8368ull},
    {10, dp3core::dysco::Distribution::kGaussian, 0.0, 0x61ADE8BBC9D10CDCull},
    {10, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0x2BCC4EC68EBF49D3ull},
    {10, dp3core::dysco::Distribution::kUniform, 0.0, 0x5A5E462E30B4DC5Eull},
    {12, dp3core::dysco::Distribution::kGaussian, 0.0, 0x722A1EA1E1DB17A8ull},
    {12, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0x1EE2DADA27077199ull},
    {12, dp3core::dysco::Distribution::kUniform, 0.0, 0xC172F5B38F12BDFEull},
    {16, dp3core::dysco::Distribution::kGaussian, 0.0, 0x0683C6B82C93753Bull},
    {16, dp3core::dysco::Distribution::kTruncatedGaussian, 2.5, 0x0BBA80066ABFD68Cull},
    {16, dp3core::dysco::Distribution::kUniform, 0.0, 0xD4CA81A61DD1E8F9ull},
    {10, dp3core::dysco::Distribution::kTruncatedGaussian, 1.5, 0xC670E9EACB25C778ull},
    {10, dp3core::dysco::Distribution::kTruncatedGaussian, 2.0, 0x8EAD63FB19155E28ull},
    {10, dp3core::dysco::Distribution::kTruncatedGaussian, 3.0, 0xAFE215D0E886B103ull},
    {10, dp3core::dysco::Distribution::kTruncatedGaussian, 4.0, 0x55531302BF629D92ull},
};

#endif
