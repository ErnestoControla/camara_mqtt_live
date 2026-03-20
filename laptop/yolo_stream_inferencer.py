#!/usr/bin/env python3
"""
Fase D: inferencia YOLO en vivo sobre el stream MJPEG de la ESP32-CAM.

- Lee el stream HTTP (mismo endpoint que stream_viewer.py).
- Ejecuta Ultralytics YOLO cada N frames (--stride) para aligerar CPU/GPU.
- Muestra video con cajas; opcionalmente publica JSON en MQTT (camara/detecciones).

Entorno: conda activate sapera_django_yolo26

Cierra el navegador si tenias /stream abierto; un solo cliente evita timeouts.
"""

from __future__ import annotations

import argparse
import json
import time
from dataclasses import dataclass
from typing import Any, Dict, List, Optional

import cv2
import paho.mqtt.client as mqtt
from ultralytics import YOLO


@dataclass(frozen=True)
class DetectionSummary:
    objects_total: int
    classes_all: List[str]
    classes_unique: List[str]

    def to_payload(self, source: str) -> Dict[str, Any]:
        return {
            "source": source,
            "objects_total": self.objects_total,
            "classes": self.classes_all,
            "classes_unique": self.classes_unique,
            "ts_epoch_ms": int(time.time() * 1000),
        }


def make_mqtt_client(client_id: str) -> mqtt.Client:
    """Paho 2.x recomienda CallbackAPIVersion.VERSION2."""
    try:
        try:
            from paho.mqtt.client import CallbackAPIVersion
        except ImportError:
            from paho.mqtt.enums import CallbackAPIVersion

        return mqtt.Client(
            client_id=client_id,
            callback_api_version=CallbackAPIVersion.VERSION2,
        )
    except (TypeError, ImportError, AttributeError):
        return mqtt.Client(client_id=client_id)


def summary_from_results(r0: Any, names: Dict[int, str]) -> DetectionSummary:
    boxes = getattr(r0, "boxes", None)
    if boxes is None or boxes.cls is None or len(boxes.cls) == 0:
        return DetectionSummary(objects_total=0, classes_all=[], classes_unique=[])

    cls_ids = boxes.cls.detach().cpu().numpy().astype(int).tolist()
    classes_all = [str(names[cid]) for cid in cls_ids]
    classes_unique = sorted(set(classes_all))
    return DetectionSummary(
        objects_total=len(cls_ids),
        classes_all=classes_all,
        classes_unique=classes_unique,
    )


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="YOLO en vivo sobre stream MJPEG (ESP32-CAM)")
    p.add_argument(
        "--url",
        default="http://192.168.100.63/stream",
        help="URL del stream MJPEG",
    )
    p.add_argument("--model", default="yolov8n.pt", help="Pesos YOLO (ultralytics)")
    p.add_argument("--conf", type=float, default=0.25)
    p.add_argument("--iou", type=float, default=0.45)
    p.add_argument(
        "--imgsz",
        type=int,
        default=640,
        help=(
            "Tamano de inferencia (lado del cuadrado interno, ej. 640). "
            "La camara VGA 640x480 se reescala con letterbox dentro de este tamano; "
            "no hace falta que el frame sea 640x640."
        ),
    )
    p.add_argument(
        "--stride",
        type=int,
        default=2,
        help="Ejecutar YOLO cada N frames capturados (1 = todos los frames; 2 o 3 aligera carga)",
    )
    p.add_argument("--window-name", default="YOLO + ESP32 stream", help="Titulo ventana OpenCV")
    p.add_argument("--reconnect-delay", type=float, default=1.0, help="Segundos entre reintentos de stream")

    p.add_argument("--no-mqtt", action="store_true", help="No publicar en MQTT")
    p.add_argument("--mqtt-host", default="127.0.0.1")
    p.add_argument("--mqtt-port", type=int, default=1883)
    p.add_argument("--mqtt-topic", default="camara/detecciones")
    p.add_argument(
        "--mqtt-min-interval",
        type=float,
        default=0.5,
        help="Segundos minimos entre publicaciones MQTT (evita saturar el broker)",
    )
    return p


def main() -> int:
    args = build_parser().parse_args()
    if args.stride < 1:
        args.stride = 1

    print(f"[yolo-stream] Modelo: {args.model} | URL: {args.url}")
    print("[yolo-stream] Tecla q = salir | Cierra el navegador si muestras el mismo /stream")

    model = YOLO(args.model)
    names: Dict[int, str] = model.names if isinstance(model.names, dict) else {i: str(n) for i, n in enumerate(model.names)}

    client: Optional[mqtt.Client] = None
    if not args.no_mqtt:
        client = make_mqtt_client("yolo_stream_inferencer")
        client.connect(args.mqtt_host, args.mqtt_port, keepalive=60)
        client.loop_start()
        print(f"[yolo-stream] MQTT -> {args.mqtt_topic} @ {args.mqtt_host}:{args.mqtt_port}")

    cap: Optional[cv2.VideoCapture] = None
    frame_idx = 0
    last_infer = None  # ultima imagen anotada (reutilizada entre inferencias)
    last_mqtt_t = 0.0
    fps_t0 = time.time()
    fps_count = 0
    display_fps = 0.0
    infer_fps = 0.0
    infer_count = 0
    infer_t0 = time.time()

    try:
        while True:
            if cap is None or not cap.isOpened():
                if cap is not None:
                    cap.release()
                cap = cv2.VideoCapture(args.url)
                if not cap.isOpened():
                    print("[yolo-stream] No se pudo abrir el stream. Reintentando...")
                    time.sleep(args.reconnect_delay)
                    continue
                print("[yolo-stream] Stream conectado")

            ok, frame = cap.read()
            if not ok or frame is None:
                print("[yolo-stream] Frame invalido. Reconectando...")
                cap.release()
                cap = None
                time.sleep(args.reconnect_delay)
                continue

            frame_idx += 1
            fps_count += 1
            now = time.time()
            if now - fps_t0 >= 1.0:
                display_fps = fps_count / (now - fps_t0)
                fps_count = 0
                fps_t0 = now

            run_infer = frame_idx % args.stride == 0
            if run_infer:
                results = model.predict(
                    source=frame,
                    conf=args.conf,
                    iou=args.iou,
                    imgsz=int(args.imgsz),
                    verbose=False,
                )
                infer_count += 1
                if now - infer_t0 >= 1.0:
                    infer_fps = infer_count / (now - infer_t0)
                    infer_count = 0
                    infer_t0 = now

                if results:
                    r0 = results[0]
                    last_infer = r0.plot()
                    summary = summary_from_results(r0, names)

                    if client is not None and (now - last_mqtt_t) >= args.mqtt_min_interval:
                        payload = summary.to_payload(args.url)
                        client.publish(args.mqtt_topic, json.dumps(payload), qos=0, retain=False)
                        last_mqtt_t = now
                else:
                    last_infer = frame.copy()

            out = last_infer if last_infer is not None else frame

            cv2.putText(
                out,
                f"FPS video: {display_fps:.1f} | FPS infer: {infer_fps:.1f} | stride={args.stride}",
                (10, 28),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.65,
                (0, 255, 0),
                2,
                cv2.LINE_AA,
            )
            cv2.imshow(args.window_name, out)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q"):
                break

    finally:
        if cap is not None:
            cap.release()
        cv2.destroyAllWindows()
        if client is not None:
            client.loop_stop()
            client.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
