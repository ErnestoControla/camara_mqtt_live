#!/usr/bin/env python3
"""
Visualizador local para el stream MJPEG de la ESP32-CAM.

- Abre una ventana con OpenCV.
- Muestra FPS aproximado en pantalla.
- Reintenta conexion automaticamente si el stream se cae.
- Permite guardar snapshot con tecla "s".

Cierra el navegador si tenias abierto /stream en la misma ESP32: varios clientes
pueden saturar el servidor y OpenCV mostrara timeout (~30 s) hasta liberar la conexion.
"""

from __future__ import annotations

import argparse
import os
import time
from datetime import datetime

import cv2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Visualizador MJPEG para ESP32-CAM")
    parser.add_argument(
        "--url",
        default="http://192.168.100.61/stream",
        help="URL MJPEG de la camara (ej: http://192.168.100.61/stream)",
    )
    parser.add_argument(
        "--window-name",
        default="ESP32-CAM Live",
        help="Titulo de la ventana",
    )
    parser.add_argument(
        "--reconnect-delay",
        type=float,
        default=1.0,
        help="Segundos de espera entre reintentos de conexion",
    )
    parser.add_argument(
        "--snapshot-dir",
        default="snapshots",
        help="Carpeta donde guardar snapshots con tecla 's'",
    )
    return parser


def open_stream(url: str) -> cv2.VideoCapture | None:
    cap = cv2.VideoCapture(url)
    if not cap.isOpened():
        cap.release()
        return None
    return cap


def ensure_dir(path: str) -> None:
    if not os.path.isdir(path):
        os.makedirs(path, exist_ok=True)


def main() -> int:
    args = build_parser().parse_args()
    ensure_dir(args.snapshot_dir)

    print(f"[INFO] Conectando a: {args.url}")
    print("[INFO] Teclas: q = salir, s = guardar snapshot")

    cap = None
    fps = 0.0
    frame_counter = 0
    last_fps_t = time.time()

    while True:
        if cap is None:
            cap = open_stream(args.url)
            if cap is None:
                print("[WARN] No se pudo abrir el stream. Reintentando...")
                time.sleep(args.reconnect_delay)
                continue
            print("[INFO] Stream conectado")
            frame_counter = 0
            last_fps_t = time.time()

        ok, frame = cap.read()
        if not ok or frame is None:
            print("[WARN] Frame invalido. Reconectando stream...")
            cap.release()
            cap = None
            time.sleep(args.reconnect_delay)
            continue

        frame_counter += 1
        now = time.time()
        elapsed = now - last_fps_t
        if elapsed >= 1.0:
            fps = frame_counter / elapsed
            frame_counter = 0
            last_fps_t = now

        overlay = f"FPS: {fps:.1f}"
        cv2.putText(
            frame,
            overlay,
            (10, 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.9,
            (0, 255, 0),
            2,
            cv2.LINE_AA,
        )
        cv2.imshow(args.window_name, frame)

        key = cv2.waitKey(1) & 0xFF
        if key == ord("q"):
            break
        if key == ord("s"):
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            out = os.path.join(args.snapshot_dir, f"snapshot_{ts}.jpg")
            cv2.imwrite(out, frame)
            print(f"[INFO] Snapshot guardado: {out}")

    if cap is not None:
        cap.release()
    cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
