"""Colab recipe for training and exporting the fixed-shape roi-analyzer model."""

# dynamic=False and nms=False are mandatory: dynamic exports contain -1 dims
# rejected by the C runtime, while embedded NMS changes the output tensor layout.

from ultralytics import YOLO


model = YOLO("yolov8n.pt")
model.train(imgsz=640, epochs=100, batch=16, device=0)
model.export(
    format="onnx",
    imgsz=640,
    opset=18,
    dynamic=False,
    simplify=True,
    nms=False,
    batch=1,
)
