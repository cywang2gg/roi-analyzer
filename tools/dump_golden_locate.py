import argparse
import hashlib
import importlib.util
import json
import os
import sys


def load_original(path):
    if not os.path.isfile(path):
        raise FileNotFoundError("Original Python source not found: " + path)
    import cv2

    cv2.imshow = lambda *args, **kwargs: None
    cv2.waitKey = lambda *args, **kwargs: 0
    cv2.destroyAllWindows = lambda *args, **kwargs: None
    spec = importlib.util.spec_from_file_location("color_image_comparator", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot import original Python source: " + path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def crop_and_scale(image, stage_a):
    import cv2

    height, width = image.shape[:2]
    if stage_a is None:
        x1 = 0
        y1 = 0
        crop = image
    else:
        box_width = float(stage_a["w"]) * 1.2
        box_height = float(stage_a["h"]) * 1.2
        x1 = int(max(0.0, min(width - 1.0,
                              float(stage_a["xc"]) - box_width / 2.0)))
        y1 = int(max(0.0, min(height - 1.0,
                              float(stage_a["yc"]) - box_height / 2.0)))
        x2 = int(max(0.0, min(float(width),
                              float(stage_a["xc"]) + box_width / 2.0)))
        y2 = int(max(0.0, min(float(height),
                              float(stage_a["yc"]) + box_height / 2.0)))
        crop = image[y1:y2, x1:x2]
    crop_height, crop_width = crop.shape[:2]
    if crop_width == 0 or crop_height == 0:
        raise ValueError("Stage B crop is empty")
    new_width = crop_width
    new_height = crop_height
    if max(crop_width, crop_height) > 400:
        scale = 400.0 / max(crop_width, crop_height)
        new_width = max(1, int(crop_width * scale))
        new_height = max(1, int(crop_height * scale))
        crop = cv2.resize(crop, (new_width, new_height),
                          interpolation=cv2.INTER_AREA)
    encoded_ok, encoded = cv2.imencode(".bmp", crop)
    if not encoded_ok:
        raise RuntimeError("OpenCV failed to encode Stage B BMP")
    return crop, {
        "x1": x1,
        "y1": y1,
        "crop_w": crop_width,
        "crop_h": crop_height,
        "new_w": new_width,
        "new_h": new_height,
        "rx": new_width / crop_width,
        "ry": new_height / crop_height,
        "bmp_sha1": hashlib.sha1(encoded.tobytes()).hexdigest(),
    }


def run_original_grid(module, image):
    function = getattr(module, "extract_patches_from_grid", None)
    if function is None:
        return None
    return function(image)


def normalize_grid_result(result):
    if not isinstance(result, dict):
        raise RuntimeError(
            "extract_patches_from_grid must return the locate dump mapping; "
            "the original function returned " + type(result).__name__)
    stage_c = result.get("stage_c")
    stage_d = result.get("stage_d")
    if stage_c is None and all(
            key in result for key in (
                "segs", "h_merged", "v_merged", "orient_swapped",
                "h_refined", "v_refined")):
        stage_c = {
            "segs": result["segs"],
            "h_merged": result["h_merged"],
            "v_merged": result["v_merged"],
            "orient_swapped": result["orient_swapped"],
            "h_refined": result["h_refined"],
            "v_refined": result["v_refined"],
        }
    if stage_d is None and "rois" in result:
        stage_d = {"rois": result["rois"], "stats": result.get("stats", [])}
    if isinstance(stage_c, dict) and isinstance(stage_d, dict):
        return stage_c, stage_d
    raise RuntimeError(
        "Original grid extractor result lacks complete stage_c/stage_d data")


def main():
    parser = argparse.ArgumentParser(
        description="Dump a headless locate golden JSON from a WIC BMP.")
    parser.add_argument("bmp", help="24-bit BMP produced by test_locate")
    parser.add_argument("-o", "--output", help="output JSON path")
    parser.add_argument(
        "--original",
        default=os.environ.get("CC_LOCATE_ORIGINAL"),
        help="path to ColorImageComparator_v8-2_d65.py "
             "(or set CC_LOCATE_ORIGINAL)")
    parser.add_argument(
        "--stage-a", default="null",
        help='JSON Stage A box, or "null" for pre-cropped input')
    args = parser.parse_args()
    if not args.original:
        parser.error("--original or CC_LOCATE_ORIGINAL is required")

    import cv2
    import numpy

    module = load_original(args.original)
    with open(args.bmp, "rb") as source:
        bmp_bytes = source.read()
    image = cv2.imdecode(numpy.frombuffer(bmp_bytes, dtype=numpy.uint8),
                         cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError("OpenCV failed to decode BMP: " + args.bmp)
    try:
        stage_a = json.loads(args.stage_a)
    except json.JSONDecodeError as error:
        raise ValueError("--stage-a must be JSON or null") from error
    stage_b_image, stage_b = crop_and_scale(image, stage_a)
    original_result = run_original_grid(module, stage_b_image)
    stage_c, stage_d = normalize_grid_result(original_result)
    with open(args.original, "rb") as original_source:
        original_sha256 = hashlib.sha256(original_source.read()).hexdigest()

    golden = {
        "header": {
            "tool": "dump_golden_locate.py",
            "python": sys.version.split()[0],
            "opencv": cv2.__version__,
            "numpy": numpy.__version__,
            "decoder": "WIC",
            "exif_orientation": 1,
            "source_pt_sha256": original_sha256,
        },
        "image": os.path.basename(args.bmp),
        "decode": {
            "w": int(image.shape[1]),
            "h": int(image.shape[0]),
            "orientation_applied": False,
            "sha1_bgr": hashlib.sha1(image.tobytes()).hexdigest(),
        },
        "stage_a": stage_a,
        "stage_b": stage_b,
        "stage_c": stage_c,
        "stage_d": stage_d,
    }
    output_path = args.output or os.path.splitext(args.bmp)[0] + ".json"
    with open(output_path, "w", encoding="utf-8", newline="\n") as output:
        json.dump(golden, output, indent=2, ensure_ascii=True)
        output.write("\n")
    print(output_path)


if __name__ == "__main__":
    main()
