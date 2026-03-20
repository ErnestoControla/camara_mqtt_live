# Fase E — Cierre y documentación (v1)

Este documento resume el cierre del MVP **camara_mqtt_live**: streaming MJPEG VGA desde ESP32-CAM, visualización en laptop e inferencia YOLO en vivo con publicación MQTT opcional.

## Resumen de arquitectura (v1)

| Capa | Componente | Rol |
|------|------------|-----|
| Edge | ESP32-CAM (OV3660 en pruebas) | VGA JPEG, servidor HTTP MJPEG, MQTT estado/IP |
| Red | WiFi 2.4 GHz LAN | Misma subred laptop ↔ ESP32 |
| Laptop | Navegador o `stream_viewer.py` | Visualización sin YOLO |
| Laptop | `yolo_stream_inferencer.py` | YOLO + ventana + MQTT `camara/detecciones` |
| Laptop | Mosquitto (Docker, ej. `camara_nodered_mqtt`) | Broker para estado y detecciones |

## Rutas HTTP (firmware)

- `GET /` — página HTML con `<img src=/stream>`
- `GET /stream` — MJPEG multipart
- `GET /snapshot` — JPEG único
- `GET /health` — texto `ok`

## Tópicos MQTT (firmware + script YOLO)

| Tópico | Publicador | Contenido |
|--------|------------|-----------|
| `camara/estado` | ESP32 | `online`, etc. |
| `camara/ip` | ESP32 | IP DHCP |
| `camara/detecciones` | Laptop (YOLO stream) | JSON: `objects_total`, `classes`, `classes_unique`, `source`, `ts_epoch_ms` |

## Protocolo de prueba de estabilidad (recomendado)

1. Flashear firmware, anotar IP en monitor o `camara/ip`.
2. **Solo un cliente** de `/stream` (cerrar otras pestañas / apps).
3. Sesión **≥ 10–15 min** con `stream_viewer.py` o navegador: sin reinicios ESP32, sin cortes prolongados.
4. Repetir con `yolo_stream_inferencer.py` (Mosquitto arriba): observar FPS en overlay y uso CPU/GPU.
5. Probar **reconexión**: apagar WiFi del router unos segundos o reiniciar ESP32; el script Python debe reconectar si se implementó (stream viewer / YOLO).

## Afinado de rendimiento

### En la laptop (sin recompilar)

- `--stride` mayor → menos inferencias/s, más fluidez si el CPU no da.
- `--imgsz 320` → más rápido, menos precisión fina.
- `--conf` más alto → menos falsos positivos.
- `--no-mqtt` → menos carga si no necesitas broker.

### En el firmware (`camara_mqtt_live.c`)

- `jpeg_quality` (sensor): valores **más altos** = más compresión = menos bits/s (mejor si el WiFi se satura).
- `vTaskDelay` en el bucle del stream: subir ms → menos FPS ESP32, menos carga.
- **Un solo cliente** sigue siendo la regla práctica más importante en ESP32-CAM.

## Limitaciones conocidas (v1)

- El servidor HTTP de la ESP32 **no está pensado para muchos clientes simultáneos**; navegador + OpenCV a la vez suele fallar o dar timeout.
- Latencia MJPEG depende de WiFi, tamaño JPEG y FPS efectivo.
- YOLO en CPU puede limitar FPS de inferencia por debajo del FPS del stream.

## Entorno Conda

- Laptop: **`conda activate sapera_django_yolo26`** para scripts en `laptop/`.
- Firmware: ESP-IDF con `get_idf` tras activar el entorno que uses para compilar.

## Estado

**MVP v1 cerrado** según plan A–E del `README.md` principal.

Mejoras futuras posibles: múltiples streams con proxy en laptop, control MQTT on/off del stream, calidad dinámica, modelo YOLO más ligero o TensorRT/GPU.
