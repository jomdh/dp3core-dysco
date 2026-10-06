#!/usr/bin/env python3
"""Write a plain MeasurementSet of synthetic Gaussian visibilities.

The skeleton is one time slot of a template MeasurementSet (TC00 by default).
This script copies that slot onto ordinary casacore columns, repeats it, then
replaces DATA, FLAG and WEIGHT_SPECTRUM. The template's antenna-pair, UVW and
Stokes I storage managers reject a second time slot written in bulk.

Layout: 38 antennas, the template's cross-correlation baselines, 64 channels,
4 polarizations, 200 times. NumPy seed 1. Ten percent of the visibilities are
NaN and flagged.
"""
import os
import shutil
import sys

import numpy as np
from casacore.tables import table


NTIME = 200
SEED = 1
NAN_FRACTION = 0.10


def copy_plain(src, dest):
    """Copy an MS and store every main-table column in StandardStMan."""
    if os.path.exists(dest):
        shutil.rmtree(dest)
    src_table = table(src, ack=False)
    columns = src_table.colnames()
    dminfo = {
        "*1": {
            "TYPE": "StandardStMan",
            "NAME": "SSM",
            "SPEC": {"BUCKETSIZE": 32768},
            "COLUMNS": columns,
        }
    }
    src_table.copy(dest, deep=True, dminfo=dminfo)
    src_table.close()


def main():
    skeleton = sys.argv[1]
    dest = sys.argv[2]
    copy_plain(skeleton, dest)
    ms = table(dest, readonly=False, ack=False)
    nbl = ms.nrows()
    data0 = ms.getcol("DATA")
    if data0.shape != (nbl, 64, 4):
        raise SystemExit(f"template DATA shape {data0.shape}, expected ({nbl}, 64, 4)")
    ant1 = ms.getcol("ANTENNA1")
    ant2 = ms.getcol("ANTENNA2")
    if ant1.shape != (nbl,) or np.any(ant1 == ant2):
        raise SystemExit("template slot is not cross-correlations only")
    nant = int(max(ant1.max(), ant2.max()) + 1)
    if nant != 38 or nbl != 703:
        raise SystemExit(f"template has {nant} antennas and {nbl} baselines, expected 38 and 703")

    templates = {}
    for name in ms.colnames():
        try:
            templates[name] = ms.getcol(name)
        except Exception as exc:
            print(f"skip {name}: {exc}", file=sys.stderr)
    interval = float(np.median(templates["INTERVAL"]))
    if interval <= 0:
        interval = 1.0
    t0 = float(templates["TIME"][0])

    ms.addrows((NTIME - 1) * nbl)
    for slot in range(1, NTIME):
        row0 = slot * nbl
        when = np.float64(t0 + slot * interval)
        for name, values in templates.items():
            column = values
            if name in ("TIME", "TIME_CENTROID"):
                column = np.full(nbl, when, dtype=values.dtype)
            ms.putcol(name, column, startrow=row0, nrow=nbl)

    rng = np.random.default_rng(SEED)
    gains = np.ones((nant, 4), np.complex64)
    gains += (
        0.05
        * (
            rng.normal(size=(nant, 4)) + 1j * rng.normal(size=(nant, 4))
        ).astype(np.complex64)
    )
    channel = np.arange(64, dtype=np.float64)
    bandpass = np.exp(-0.5 * ((channel - 32.0) / 20.0) ** 2).astype(np.float32)
    signal = gains[ant1] * np.conj(gains[ant2])
    signal = (signal[:, None, :] * bandpass[None, :, None]).astype(np.complex64)
    weights = np.broadcast_to(
        (0.5 + 0.5 * bandpass).astype(np.float32)[None, :, None], (nbl, 64, 4)
    ).copy()
    scale = np.float32(1.0 / np.sqrt(2.0))
    for slot in range(NTIME):
        row0 = slot * nbl
        noise = scale * (
            rng.normal(size=(nbl, 64, 4)).astype(np.float32)
            + 1j * rng.normal(size=(nbl, 64, 4)).astype(np.float32)
        )
        data = (signal + noise).astype(np.complex64)
        nan_mask = rng.random((nbl, 64, 4)) < NAN_FRACTION
        data[nan_mask] = np.nan
        ms.putcol("DATA", data, startrow=row0, nrow=nbl)
        ms.putcol("FLAG", nan_mask, startrow=row0, nrow=nbl)
        ms.putcol("WEIGHT_SPECTRUM", weights, startrow=row0, nrow=nbl)
        ms.putcol("FLAG_ROW", np.zeros(nbl, dtype=bool), startrow=row0, nrow=nbl)
        if slot % 50 == 0:
            print(f"filled slot {slot}", flush=True)
    ms.flush()
    ms.close()

    check = table(dest, ack=False)
    if check.nrows() != NTIME * nbl:
        raise SystemExit(f"row count {check.nrows()}, expected {NTIME * nbl}")
    sample = check.getcol("DATA", startrow=0, nrow=nbl)
    if sample.shape != (nbl, 64, 4) or not np.isnan(sample).any():
        raise SystemExit("first slot DATA is missing the NaN mask")
    times = check.getcol("TIME")
    if len(np.unique(times)) != NTIME:
        raise SystemExit(f"expected {NTIME} distinct times, found {len(np.unique(times))}")
    check.close()
    print(f"synthetic rows={NTIME * nbl} baselines={nbl} antennas={nant} times={NTIME}")


if __name__ == "__main__":
    main()
