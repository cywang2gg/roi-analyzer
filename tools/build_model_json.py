"""Build an roi-analyzer model sidecar from a validated ONNX model."""

import argparse
import json
import os
import sys

import onnx

from model_check import validate_model


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", help="validated ONNX model")
    parser.add_argument("--names", nargs="+", required=True, metavar="NAME")
    parser.add_argument("--output", required=True, help="output JSON path")
    parser.add_argument("--v1-class", type=int, default=1)
    parser.add_argument("--pvt-class", type=int, default=3)
    parser.add_argument("--source", default=None)
    args = parser.parse_args()

    result = validate_model(args.model)
    details = result["details"]
    if not details.get("all_hard_checks_passed"):
        failures = [check["message"] for check in result["checks"]
                    if not check["passed"] and check["name"] != "ONNX opset"]
        print("MODEL-JSON: FAIL: model validation failed: {}".format(
            "; ".join(failures) if failures else "unknown validation failure"
        ), file=sys.stderr)
        return 1

    metadata_names = details["names"]
    if args.names != metadata_names:
        print("MODEL-JSON: FAIL: --names does not match ONNX metadata", file=sys.stderr)
        print("  --names:  {}".format(args.names), file=sys.stderr)
        print("  metadata: {}".format(metadata_names), file=sys.stderr)
        return 1
    if not 0 <= args.v1_class < len(metadata_names):
        print("MODEL-JSON: FAIL: --v1-class {} does not identify an existing class".format(
            args.v1_class
        ), file=sys.stderr)
        return 1
    if not 0 <= args.pvt_class < len(metadata_names):
        print("MODEL-JSON: FAIL: --pvt-class {} does not identify an existing class".format(
            args.pvt_class
        ), file=sys.stderr)
        return 1

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    live_sidecar = os.path.join(repo_root, "models", "color_chart.json")
    output_path = os.path.abspath(args.output)
    if os.path.normcase(output_path) == os.path.normcase(live_sidecar):
        print("MODEL-JSON: FAIL: refusing to overwrite models/color_chart.json", file=sys.stderr)
        return 1

    model = onnx.load(args.model)
    producer = " ".join(
        part for part in (model.producer_name, model.producer_version) if part
    )
    source = args.source if args.source is not None else os.path.basename(args.model)
    sidecar = {
        "source": source,
        "source_sha256": details["sha256"],
        "ultralytics_or_producer": producer,
        "yolo_family": "v8",
        "task": "detect",
        "imgsz": 640,
        "names": metadata_names,
        "v1_use_class": args.v1_class,
        "pvt_use_class": args.pvt_class,
        "output_shape": details["output_shape"],
        "opset": details["opset"],
    }

    try:
        parent = os.path.dirname(output_path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(output_path, "w", encoding="utf-8", newline="\n") as output_file:
            json.dump(sidecar, output_file, indent=4, ensure_ascii=False)
            output_file.write("\n")
    except OSError as exc:
        print("MODEL-JSON: FAIL: unable to write {}: {}".format(output_path, exc),
              file=sys.stderr)
        return 1
    print("MODEL-JSON: PASS: {}".format(output_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
