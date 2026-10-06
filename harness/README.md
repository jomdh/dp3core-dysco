# Casacore equivalence harness

`dp3core_dysco_p2` compares this library with casacore DyscoStMan on one MeasurementSet and several Dysco copies of it.

The copies come from DP3:

```
numthreads=1 steps=[] msout.storagemanager=dysco
msout.storagemanager.databitrate=<bits>
msout.storagemanager.weightbitrate=12
msout.storagemanager.distribution=TruncatedGaussian
msout.storagemanager.disttruncation=2.5
msout.storagemanager.normalization=AF
```

Build with `-DDP3CORE_DYSCO_HARNESS=ON`. The target needs casacore. It uses GSL when CMake finds it.

```
dp3core_dysco_p2 --input in.MS --casacore run1.MS run2.MS run3.MS run4.MS run5.MS --out reports/name
```

Exit 0 means every check passed. `PASS_EXPLAIN` counts as passed. Exit 1 means a check failed or was not evaluated. Exit 2 means a usage or I/O error. The report is `report.md` and `report.json` in `--out`. A side-data difference is listed in `side-data-diff.csv`.

`max_over_sigma` is the maximum farther-centroid error divided by sigma. Both sides take it from the normalization side data and the shared dictionary. A dither draw is not part of that maximum. The report also lists the realized dithered peak.

`make_synthetic_ms.py SKELETON.MS OUT.MS` repeats one template time slot into 200 times and writes synthetic visibilities. `run_p2.sh` on the Mac copies this repository into the gate mount and runs `run_p2_inner.sh` in `dp3gate`.

```
DATASETS=synthetic BITS=10 harness/run_p2.sh
```

`DATASETS` accepts `synthetic` and `tc00`. `BITS` accepts any subset of `6 8 10 12 16`.

Each encode uses one time slot as one chunk. The Docker VM does not have room for a full TC00 array beside the casacore tables. Side data does not depend on other slots. The dither nonce is the nonce of that one slot.
