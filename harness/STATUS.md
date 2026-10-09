# P2 harness status

Re-run 9 Oct 2026 inside `dp3gate`. Codec tip `f3a1228`.
`DATASETS='synthetic tc00'` and `BITS=10`. No PyPI release and no
zarr-extensions pull request.

Synthetic input: one TC00 time slot repeated to 200 slots. The
harness exited 0. The input sha256 was unchanged.

TC00 input: the e2e `TC00.MS`. The harness exited 0. `SHA256SUMS`
lists `TC00.MS.zip`, which is not in this tree. That is the one
non-OK line before and after the run.

| Setting | Exit | Checks | ours rms/sigma | max/sigma |
| --- | --- | --- | --- | --- |
| synthetic, 10 data bits, 12 weight bits | 0 | all PASS | 0.0030310912607961846 | 0.094165096916374452 |
| TC00, 10 data bits, 12 weight bits | 0 | all PASS | 0.0025584700941380101 | 0.1631310612548596 |

Reports: `DP3Core-build/bench/p2/reports/synthetic-d10/report.md`
and `tc00-d10/report.md`.
