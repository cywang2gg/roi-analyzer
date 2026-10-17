"""Validate an ONNX model for the roi-analyzer YOLOv8 runtime."""

import argparse
import ast
import hashlib
import json
import sys

import onnx
import onnxruntime as ort
import numpy as np


def _shape(value_info):
    dims = []
    for dim in value_info.type.tensor_type.shape.dim:
        if dim.HasField("dim_value"):
            dims.append(dim.dim_value)
        elif dim.dim_param:
            dims.append(dim.dim_param)
        else:
            dims.append(None)
    return dims


def validate_model(model_path, run_inference=True):
    """Return hard-check results and model details; never suppress validation errors."""
    checks = []
    details = {
        "input_shape": None,
        "output_shape": None,
        "runtime_output_shape": None,
        "names": None,
        "opset": None,
        "producer": None,
        "sha256": None,
        "class_indices": {},
    }

    def record(name, passed, message):
        checks.append({"name": name, "passed": bool(passed), "message": message})

    try:
        with open(model_path, "rb") as model_file:
            digest = hashlib.sha256()
            for chunk in iter(lambda: model_file.read(1024 * 1024), b""):
                digest.update(chunk)
        details["sha256"] = digest.hexdigest()
        model = onnx.load(model_path)
        onnx.checker.check_model(model)
    except Exception as exc:
        record("load ONNX model", False, str(exc))
        details["all_hard_checks_passed"] = False
        return {"checks": checks, "details": details}

    details["producer"] = " ".join(
        part for part in (model.producer_name, model.producer_version) if part
    )

    inputs = [value for value in model.graph.input if value.name == "images"]
    if len(inputs) != 1:
        record("input images", False, "expected exactly one input named 'images'")
        input_ok = False
    else:
        input_info = inputs[0]
        input_shape = _shape(input_info)
        details["input_shape"] = input_shape
        input_type = input_info.type.tensor_type.elem_type
        fixed = all(isinstance(dim, int) and dim > 0 for dim in input_shape)
        input_ok = (
            input_shape == [1, 3, 640, 640]
            and fixed
            and input_type == onnx.TensorProto.FLOAT
        )
        if not fixed:
            message = "non-fixed input dim (re-export with dynamic=False)"
        elif input_type != onnx.TensorProto.FLOAT:
            message = "input 'images' must be float32"
        else:
            message = "input 'images' must be float32 with shape [1, 3, 640, 640]"
        record("input images", input_ok, "float32 [1, 3, 640, 640]" if input_ok else message)

    metadata = {entry.key: entry.value for entry in model.metadata_props}
    names = None
    names_ok = False
    if "names" in metadata:
        try:
            parsed_names = ast.literal_eval(metadata["names"])
            if isinstance(parsed_names, dict):
                indices = sorted(parsed_names)
                if all(type(index) is int for index in indices) and \
                        indices == list(range(len(parsed_names))) and all(
                    isinstance(parsed_names[index], str) for index in indices
                ):
                    names = [parsed_names[index] for index in indices]
                    names_ok = True
        except (SyntaxError, ValueError, TypeError):
            names = None
    details["names"] = names
    nc = len(names) if names is not None else 0
    record(
        "metadata names",
        names_ok,
        "valid literal class-name dictionary" if names_ok else
        "metadata_props must contain names as an indexed dict literal",
    )

    task_ok = metadata.get("task") == "detect"
    record("metadata task", task_ok, "task=detect" if task_ok else "metadata task must be detect")
    end2end_ok = metadata.get("end2end", "").strip().lower() == "false"
    record(
        "metadata end2end",
        end2end_ok,
        "end2end=False" if end2end_ok else "metadata end2end must be False",
    )

    nc_ok = nc >= 2
    record("class count", nc_ok, "nc >= 2" if nc_ok else "nc must be >= 2")
    color_ok = names is not None and "color-chart" in names
    pvt_ok = names is not None and "pvt-target" in names
    if color_ok:
        details["class_indices"]["color-chart"] = names.index("color-chart")
    if pvt_ok:
        details["class_indices"]["pvt-target"] = names.index("pvt-target")
    record(
        "required classes",
        color_ok and pvt_ok,
        "color-chart={} pvt-target={}".format(
            details["class_indices"].get("color-chart", "missing"),
            details["class_indices"].get("pvt-target", "missing"),
        ),
    )

    outputs = [value for value in model.graph.output if value.name == "output0"]
    output_shape = _shape(outputs[0]) if len(outputs) == 1 else None
    details["output_shape"] = output_shape
    output_ok = output_shape == [1, 4 + nc, 8400] and nc > 0
    record(
        "output output0",
        output_ok,
        "output0 {}".format(output_shape) if output_ok else
        "output0 must have shape [1, 4+nc, 8400] (nc={})".format(nc),
    )

    forbidden = sorted(
        {node.op_type for node in model.graph.node
         if node.op_type in ("NonMaxSuppression", "TopK")}
    )
    record(
        "raw detection graph",
        not forbidden,
        "no NonMaxSuppression or TopK nodes" if not forbidden else
        "forbidden graph nodes: {}".format(", ".join(forbidden)),
    )

    opsets = {item.domain: item.version for item in model.opset_import}
    opset = opsets.get("", opsets.get("ai.onnx"))
    details["opset"] = opset
    record("ONNX opset", True, "opset={}".format(opset))

    runtime_ok = True
    if run_inference:
        try:
            session = ort.InferenceSession(
                model_path, providers=["CPUExecutionProvider"]
            )
            runtime_inputs = session.get_inputs()
            runtime_input_ok = (
                len(runtime_inputs) == 1
                and runtime_inputs[0].name == "images"
                and runtime_inputs[0].type == "tensor(float)"
                and runtime_inputs[0].shape == [1, 3, 640, 640]
            )
            record(
                "runtime input",
                runtime_input_ok,
                "float32 [1, 3, 640, 640]" if runtime_input_ok else
                "runtime input must be float32 [1, 3, 640, 640]",
            )
            actual = session.run(["output0"], {
                "images": np.zeros((1, 3, 640, 640), dtype=np.float32)
            })[0]
            actual_shape = list(actual.shape)
            details["runtime_output_shape"] = actual_shape
            actual_ok = output_shape is not None and actual_shape == output_shape
            record(
                "runtime inference",
                actual_ok,
                "actual output shape {}".format(actual_shape) if actual_ok else
                "runtime output shape {} does not match declared {}".format(
                    actual_shape, output_shape
                ),
            )
            runtime_ok = runtime_input_ok and actual_ok
        except Exception as exc:
            record("runtime inference", False, str(exc))
            runtime_ok = False

    details["all_hard_checks_passed"] = all(
        item["passed"] for item in checks
        if item["name"] != "ONNX opset"
    ) and runtime_ok
    return {"checks": checks, "details": details}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", help="ONNX model to validate")
    parser.add_argument("--json", action="store_true", help="print machine-readable results")
    args = parser.parse_args()

    result = validate_model(args.model)
    details = result["details"]
    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=True))
    else:
        print("Check                         Result  Detail")
        print("----------------------------  ------  ----------------------------------------")
        for check in result["checks"]:
            if check["name"] == "ONNX opset" and details.get("opset") != 18:
                status = "WARN"
            else:
                status = "PASS" if check["passed"] else "FAIL"
            print("{:<28}  {:<6}  {}".format(
                check["name"], status, check["message"]
            ))
        print("Declared input shape: {}".format(details["input_shape"]))
        print("Declared output shape: {}".format(details["output_shape"]))
        print("Runtime output shape: {}".format(details["runtime_output_shape"]))
        print("Model SHA256: {}".format(
            details["sha256"][:16] if details["sha256"] else "unavailable"
        ))
        print("Class indices: {}".format(details["class_indices"]))

    if args.json and details.get("opset") != 18:
        print("WARN: ONNX opset is {}; expected 18".format(details.get("opset")))
    if details["all_hard_checks_passed"]:
        print("MODEL-CHECK: PASS")
        return 0
    failures = [item["message"] for item in result["checks"]
                if not item["passed"] and item["name"] != "ONNX opset"]
    reason = failures[0] if failures else "validation failed"
    print("MODEL-CHECK: FAIL: {}".format(reason))
    return 1


if __name__ == "__main__":
    sys.exit(main())
