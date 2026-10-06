#!/usr/bin/env python3
"""Write rows.md from the Zenodo row list. Numbers only, no summary adjective."""
import argparse
import json
import sys


def num(value, fmt):
    if value is None:
        return ""
    return format(float(value), fmt)


def n_float(dataset, column):
    if column == "DATA":
        return dataset["n_float_data"]
    return dataset["n_float_ws"]


def plain_bytes(dataset, column):
    if column == "DATA":
        return dataset.get("plain_data_bytes") or dataset["raw_data_bytes"]
    return dataset.get("plain_ws_bytes") or dataset["raw_ws_bytes"]


def ratio(plain, nbytes):
    if plain is None or nbytes is None or nbytes == 0:
        return None
    return plain / nbytes


def bits_per_float(nbytes, nfloat):
    if nbytes is None or not nfloat:
        return None
    return nbytes * 8 / nfloat


def data_line(row, dataset):
    nbytes = row.get("bytes")
    plain = plain_bytes(dataset, "DATA")
    cells = [
        row["name"],
        num(nbytes / 1e6 if nbytes is not None else None, ".6f"),
        num(ratio(plain, nbytes), ".6f"),
        num(bits_per_float(nbytes, n_float(dataset, "DATA")), ".4f"),
        num(row.get("encode_s"), ".3f"),
        num(row.get("decode_s"), ".3f"),
        num(row.get("rms_over_sigma"), ".8g"),
        num(row.get("p999_over_sigma"), ".8g"),
        num(row.get("max_over_sigma"), ".8g"),
        num(row.get("mean_over_sigma"), ".8g"),
        num(row.get("noise_increase_pct"), ".8g"),
    ]
    return "| " + " | ".join(cells) + " |"


def weight_line(row, dataset):
    nbytes = row.get("bytes")
    plain = plain_bytes(dataset, "WEIGHT_SPECTRUM")
    cells = [
        row["name"],
        num(nbytes / 1e6 if nbytes is not None else None, ".6f"),
        num(ratio(plain, nbytes), ".6f"),
        num(bits_per_float(nbytes, n_float(dataset, "WEIGHT_SPECTRUM")), ".4f"),
        num(row.get("encode_s"), ".3f"),
        num(row.get("decode_s"), ".3f"),
        num(row.get("ws_rms_rel"), ".8g"),
        num(row.get("ws_max_rel"), ".8g"),
    ]
    return "| " + " | ".join(cells) + " |"


def render(doc):
    dataset = doc["dataset"]
    lines = []
    lines.append("# Zenodo 1044019 (clean input)")
    lines.append("")
    lines.append("Run date: %s." % doc.get("run_date", ""))
    lines.append("DP3: %s." % doc.get("dp3_version", ""))
    lines.append("Library version: %s." % doc.get("library_version", ""))
    lines.append(
        "Dataset: nrow=%s nchan=%s npol=%s nbl=%s cross=%s ntime=%s regular=%s flagged=%s."
        % (dataset.get("nrow"), dataset.get("nchan"), dataset.get("npol"), dataset.get("nbl"),
           dataset.get("n_cross_bl"), dataset.get("ntime"), dataset.get("regular"),
           dataset.get("flagged_fraction")))
    noise = doc.get("noise") or {}
    if noise:
        lines.append("Sigma median (cross, channel-difference MAD): %s. p10/p90: %s."
                     % (noise.get("sigma_cross_median"), noise.get("sigma_cross_p10_p90")))
    lines.append(
        "Casacore encode s is the DP3 process wall time. Casacore decode s is the msextract read of that column. "
        "dp3core encode s and decode s are the codec calls. dp3core bytes are the sum of the encoded chunks, including each 80-byte header.")
    lines.append(
        "Errors are decoded minus the plain DP3 copy, on unflagged cross-correlations. "
        "DATA errors are divided by the shared per-baseline per-polarisation sigma. "
        "Both encoders use AF, truncated Gaussian 2.5, and seed 0 for dp3core. "
        "Weight bitrate on the casacore DATA runs is 12.")
    lines.append("")
    lines.append("## DATA")
    lines.append("")
    lines.append("| setting | DATA MB | ratio vs plain | bits/float | encode s | decode s | RMS/σ | p99.9/σ | max/σ | mean/σ | noise increase % |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|---|")
    data_rows = [row for row in doc["rows"] if row.get("column") == "DATA" and "error" not in row]
    for row in data_rows:
        lines.append(data_line(row, dataset))
    lines.append("")
    lines.append("## Weight rows")
    lines.append("")
    lines.append("| setting | MB | ratio vs plain | bits/float | encode s | decode s | rms rel | max rel |")
    lines.append("|---|---|---|---|---|---|---|---|")
    weight_rows = [row for row in doc["rows"] if row.get("column") == "WEIGHT_SPECTRUM" and "error" not in row]
    for row in weight_rows:
        lines.append(weight_line(row, dataset))
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("doc_json")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    with open(args.doc_json) as fh:
        doc = json.load(fh)
    text = render(doc)
    with open(args.out, "w") as fh:
        fh.write(text)
    sys.stdout.write(text)


if __name__ == "__main__":
    main()
