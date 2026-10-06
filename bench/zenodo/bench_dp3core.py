#!/usr/bin/env python3
"""Rows for the Zenodo 1044019 Dysco benchmark.

Casacore rows go through bench.py's compare_dumps, ms_dm_sizes, and ErrAcc.
dp3core rows go through dp3core_dysco_bench, then the same ErrAcc.
Sigma comes from bench.py's estimate_noise and is saved once as sigma_bl_pol.npy.
"""
import argparse
import json
import os
import re
import subprocess
import sys

import numpy as np

BENCH_SRC = os.environ.get(
    "BENCH_SRC", "/data/DP3Core-build/bench/compression/src")
sys.path.insert(0, BENCH_SRC)
import bench as B  # noqa: E402


def log(msg):
    print(msg, flush=True)


def load_orig(path):
    orig = B.Dumps(path)
    geom = B.Geometry(orig, 0)
    return orig, geom


def read_json(path):
    with open(path) as fh:
        return json.load(fh)


def write_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(obj, fh, indent=1, default=float)
        fh.write("\n")
    os.replace(tmp, path)


def info_float(dump, key):
    info = B.read_info(dump)
    # read_info only casts the shape keys. Read the timing lines ourselves.
    del info
    with open(os.path.join(dump, "extract_info.txt")) as fh:
        for line in fh:
            parts = line.split()
            if len(parts) >= 2 and parts[0] == key:
                return float(parts[1])
    return None


def column_bytes(dm, column):
    exact = [item for item in dm if item["columns"] == [column]]
    if exact:
        return exact[0]["bytes"], exact[0]["type"]
    grouped = [item for item in dm if column in item["columns"]]
    if grouped:
        names = ",".join(grouped[0]["columns"])
        return grouped[0]["bytes"], grouped[0]["type"] + " group " + names
    return None, ""


def append_row(path, row):
    rows = read_json(path) if os.path.exists(path) else []
    rows = [item for item in rows if item.get("name") != row["name"] or item.get("column") != row["column"]]
    rows.append(row)
    write_json(path, rows)
    log("row " + row["name"] + " " + row["column"] + " bytes=" + str(row.get("bytes")))


def dataset_fields(orig, geom):
    flagged = 0
    total = 0
    cross_flagged = 0
    cross_total = 0
    for s0, s1 in B.slabs(orig.nrow, geom.slab_rows):
        fl = (np.asarray(orig.flag[s0:s1]) != 0) | orig.flagrow[s0:s1, None, None]
        flagged += int(fl.sum())
        total += int(fl.size)
        rc = geom.row_cross[s0:s1]
        cross_flagged += int(fl[rc].sum())
        cross_total += int(rc.sum()) * fl.shape[1] * fl.shape[2]
    return {
        "nrow": orig.nrow,
        "nchan": orig.nchan,
        "npol": orig.npol,
        "nbl": geom.nbl,
        "n_cross_bl": int(geom.bl_cross.sum()),
        "regular": bool(geom.regular),
        "ntime": int(orig.nrow // geom.nbl) if geom.nbl and orig.nrow % geom.nbl == 0 else None,
        "flagged_fraction": flagged / total if total else None,
        "flagged_fraction_cross": cross_flagged / cross_total if cross_total else None,
        "raw_data_bytes": orig.nrow * orig.nchan * orig.npol * 8,
        "raw_ws_bytes": orig.nrow * orig.nchan * orig.npol * 4 if orig.ws is not None else 0,
        "n_float_data": orig.nrow * orig.nchan * orig.npol * 2,
        "n_float_ws": orig.nrow * orig.nchan * orig.npol,
    }


def cmd_sigma(args):
    orig, geom = load_orig(args.orig)
    fields = dataset_fields(orig, geom)
    log("dataset " + json.dumps(fields))
    if not fields["regular"]:
        raise SystemExit("input is not a regular time-by-baseline grid; the bench encoder requires that order")
    sig_path = os.path.join(args.work, "sigma_bl_pol.npy")
    noise_path = os.path.join(args.work, "noise.json")
    if os.path.exists(sig_path) and os.path.exists(noise_path) and not args.force:
        log("sigma exists " + sig_path)
        sig = np.load(sig_path)
    else:
        log("estimating sigma")
        sig, noise = B.estimate_noise(orig, geom, log)
        np.save(sig_path, sig)
        write_json(noise_path, noise)
        log("sigma written " + sig_path)
    if sig.shape != (geom.nbl, orig.npol):
        raise SystemExit("sigma shape %s does not match baselines %s pols %s" % (sig.shape, geom.nbl, orig.npol))
    dataset_path = os.path.join(args.work, "dataset.json")
    existing = read_json(dataset_path) if os.path.exists(dataset_path) else {}
    existing.update(fields)
    write_json(dataset_path, existing)
    return 0


def cmd_sizes(args):
    dm = B.ms_dm_sizes(args.ms, args.dminfo)
    out = {"ms_bytes": B.dirsize(args.ms), "dm": dm}
    write_json(args.out, out)
    log("sizes " + args.out + " ms_bytes=" + str(out["ms_bytes"]))
    return 0


def attach_common(name, column, nbytes, encode_s, decode_s, res):
    row = {
        "name": name,
        "column": column,
        "bytes": nbytes,
        "encode_s": encode_s,
        "decode_s": decode_s,
        "rms_over_sigma": res.get("rms_over_sigma"),
        "p999_over_sigma": res.get("p999_over_sigma"),
        "max_over_sigma": res.get("max_over_sigma"),
        "mean_over_sigma": res.get("mean_over_sigma"),
        "noise_increase_pct": res.get("noise_increase_pct"),
        "ws_rms_rel": res.get("ws_rms_rel"),
        "ws_max_rel": res.get("ws_max_rel"),
        "n_cross_components": res.get("n_cross_components"),
        "n_nonfinite": res.get("n_nonfinite"),
    }
    if "error" in res:
        row["error"] = res["error"]
    return row


def cmd_casacore(args):
    orig, geom = load_orig(args.orig)
    sig = np.load(os.path.join(args.work, "sigma_bl_pol.npy"))
    sizes = read_json(args.sizes)
    res = B.compare_dumps(orig, geom, sig, args.dump, log)
    if "error" in res:
        log("compare error " + res["error"])
        raise SystemExit(1)
    data_b, data_how = column_bytes(sizes["dm"], "DATA")
    ws_b, ws_how = column_bytes(sizes["dm"], "WEIGHT_SPECTRUM")
    dataset = read_json(os.path.join(args.work, "dataset.json"))
    if args.weight_only:
        if ws_b is None:
            plain_ms = dataset.get("plain_ms_bytes")
            if plain_ms is None or sizes.get("ms_bytes") is None:
                raise SystemExit("no WEIGHT_SPECTRUM size for " + args.name)
            ws_b = sizes["ms_bytes"] - plain_ms + dataset["raw_ws_bytes"]
            ws_how = "ms minus plain ms plus raw weights"
        row = attach_common(args.name, "WEIGHT_SPECTRUM", ws_b, args.encode_s, info_float(args.dump, "ws_s"), res)
        row["bytes_method"] = ws_how
        append_row(args.rows, row)
        return 0
    if data_b is None:
        raise SystemExit("no DATA size for " + args.name)
    data_row = attach_common(args.name, "DATA", data_b, args.encode_s, info_float(args.dump, "data_s"), res)
    data_row["bytes_method"] = data_how
    append_row(args.rows, data_row)
    if ws_b is not None:
        weight = attach_common(args.name + " weights", "WEIGHT_SPECTRUM", ws_b, args.encode_s,
                                info_float(args.dump, "ws_s"), res)
        weight["bytes_method"] = ws_how
        append_row(args.rows, weight)
    if args.plain:
        dataset["plain_data_bytes"] = data_b
        dataset["plain_ws_bytes"] = ws_b
        dataset["plain_ms_bytes"] = sizes.get("ms_bytes")
        write_json(os.path.join(args.work, "dataset.json"), dataset)
    return 0


def compare_decoded(orig, geom, sig, path, kind):
    acc = B.ErrAcc()
    if kind == "data":
        decoded = np.memmap(path, dtype=np.complex64, mode="r", shape=(orig.nrow, orig.nchan, orig.npol))
        for s0, s1 in B.slabs(orig.nrow, geom.slab_rows):
            od = np.asarray(orig.data[s0:s1])
            acc.add(od, np.asarray(decoded[s0:s1]), B.valid_mask(orig, s0, s1, od),
                    geom.row_cross[s0:s1], sig[geom.blid[s0:s1]])
    elif kind == "weights":
        decoded = np.memmap(path, dtype=np.float32, mode="r", shape=(orig.nrow, orig.nchan, orig.npol))
        for s0, s1 in B.slabs(orig.nrow, geom.slab_rows):
            od = np.asarray(orig.data[s0:s1])
            acc.add_w(np.asarray(orig.ws[s0:s1]), np.asarray(decoded[s0:s1]), B.valid_mask(orig, s0, s1, od))
    else:
        raise SystemExit("kind must be data or weights")
    del decoded
    return acc.result()


def cmd_ours(args):
    orig, geom = load_orig(args.orig)
    sig = np.load(os.path.join(args.work, "sigma_bl_pol.npy"))
    decoded = os.path.join(args.work, "decoded-%s.bin" % args.name.replace(" ", "_"))
    timing_path = decoded + ".json"
    cmd = [args.bin, "--dump", args.orig, "--kind", args.kind, "--bits", str(args.bits),
           "--out-decoded", decoded, "--json", timing_path, "--apply-writer-policy"]
    log(" ".join(cmd))
    subprocess.check_call(cmd)
    timing = read_json(timing_path)
    res = compare_decoded(orig, geom, sig, decoded, args.kind)
    os.remove(decoded)
    column = "DATA" if args.kind == "data" else "WEIGHT_SPECTRUM"
    row = attach_common(args.name, column, timing["encoded_bytes"], timing["encode_s"], timing["decode_s"], res)
    row["bytes_method"] = "sum of encoded chunk sizes, including the 80-byte header"
    row["writer_policy"] = timing.get("writer_policy")
    row["bits"] = timing.get("bits")
    append_row(args.rows, row)
    return 0


def library_version(cmake_path):
    text = open(cmake_path).read()
    match = re.search(r"project\(dp3core_dysco VERSION ([0-9.]+)", text)
    if not match:
        raise SystemExit("library version not found in " + cmake_path)
    return match.group(1)


def env_value(path, prefix):
    with open(path) as fh:
        for line in fh:
            if line.startswith(prefix):
                return line.split(":", 1)[1].strip()
    return ""


def cmd_finish(args):
    dataset = read_json(os.path.join(args.work, "dataset.json"))
    noise_path = os.path.join(args.work, "noise.json")
    noise = read_json(noise_path) if os.path.exists(noise_path) else {}
    doc = {
        "run_date": env_value(args.env, "date:")[:10],
        "dp3_version": env_value(args.env, "DP3:"),
        "library_version": library_version(args.cmake),
        "dataset": dataset,
        "noise": noise,
        "rows": read_json(args.rows),
    }
    write_json(args.out, doc)
    log("doc " + args.out + " rows=" + str(len(doc["rows"])))
    return 0


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    sigma = sub.add_parser("sigma")
    sigma.add_argument("--orig", required=True)
    sigma.add_argument("--work", required=True)
    sigma.add_argument("--force", action="store_true")
    sigma.set_defaults(func=cmd_sigma)

    sizes = sub.add_parser("sizes")
    sizes.add_argument("--ms", required=True)
    sizes.add_argument("--dminfo", required=True)
    sizes.add_argument("--out", required=True)
    sizes.set_defaults(func=cmd_sizes)

    casa = sub.add_parser("casacore")
    casa.add_argument("--orig", required=True)
    casa.add_argument("--work", required=True)
    casa.add_argument("--dump", required=True)
    casa.add_argument("--sizes", required=True)
    casa.add_argument("--rows", required=True)
    casa.add_argument("--name", required=True)
    casa.add_argument("--encode-s", type=float, required=True)
    casa.add_argument("--plain", action="store_true")
    casa.add_argument("--weight-only", action="store_true")
    casa.set_defaults(func=cmd_casacore)

    ours = sub.add_parser("ours")
    ours.add_argument("--orig", required=True)
    ours.add_argument("--work", required=True)
    ours.add_argument("--rows", required=True)
    ours.add_argument("--name", required=True)
    ours.add_argument("--bin", required=True)
    ours.add_argument("--bits", type=int, required=True)
    ours.add_argument("--kind", choices=("data", "weights"), required=True)
    ours.set_defaults(func=cmd_ours)

    finish = sub.add_parser("finish")
    finish.add_argument("--work", required=True)
    finish.add_argument("--rows", required=True)
    finish.add_argument("--env", required=True)
    finish.add_argument("--cmake", required=True)
    finish.add_argument("--out", required=True)
    finish.set_defaults(func=cmd_finish)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main() or 0)
