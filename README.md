# camara_mqtt_live

Proyecto **MVP v1 (cerrado)**: streaming **MJPEG VGA** desde ESP32-CAM, visualización en laptop e **inferencia YOLO en vivo** con MQTT opcional. Evoluciona la idea de `camara_nodered_mqtt` (foto por disparo) hacia **video continuo**.

Documentación de cierre ampliada: [`docs/fase-e-cierre.md`](docs/fase-e-cierre.md).

## Objetivo general

Implementar un flujo en dos etapas:

1. **Etapa 1 (MVP de streaming):** capturar video de la ESP32-CAM en **VGA (640x480)** y visualizarlo en la laptop en tiempo real.
2. **Etapa 2 (IA en vivo):** tomar el stream de video y ejecutar detección con YOLO sobre los frames.

## Base técnica del proyecto

Se toma como referencia `camara_nodered_mqtt` porque ya resuelve correctamente:

- Inicialización de cámara para la tarjeta actual.
- Conectividad WiFi estable en la red de trabajo.
- Integración MQTT para control/estado.
- Envío de imágenes hacia la laptop para procesamiento.

Este proyecto cambia el enfoque de **foto estática por disparo** a **flujo continuo de video**.

## Alcance por etapas

### Etapa 1 - Streaming VGA ESP32 -> Laptop (sin YOLO)

**Meta:** ver video en vivo desde la cámara en la laptop.

#### Entregables

- Firmware ESP32-CAM capaz de capturar frames VGA de forma continua.
- Transporte del stream hacia la laptop (priorizar método estable y simple).
- Visualización en tiempo real en:
  - navegador web, o
  - ventana emergente de escritorio (script en Python/OpenCV).
- Medición inicial de FPS, latencia y estabilidad.

#### Criterios de aceptación

- Se visualiza video en la laptop durante al menos 5 minutos sin caídas críticas.
- Resolución de salida: VGA (640x480).
- Latencia percibida aceptable para monitoreo en vivo (objetivo inicial: < 1 s).

### Etapa 2 - Detección YOLO sobre video en vivo

**Meta:** ejecutar detecciones en el stream y mostrar resultados en tiempo real.

#### Entregables

- Pipeline en laptop para leer stream en vivo y procesar frames con YOLO.
- Visualización con bounding boxes, clase y confianza.
- Publicación opcional de resultados por MQTT (conteo/clases por frame o por ventana de tiempo).
- Parámetros de rendimiento configurables (tamaño de frame, tasa de inferencia, umbral).

#### Criterios de aceptación

- Detecciones visibles sobre video en una ventana o interfaz web.
- El sistema mantiene flujo continuo con degradación controlada (frame skipping si hace falta).
- Se documentan límites de rendimiento en hardware actual.

## Plan de trabajo propuesto

### Fase A - Definición de arquitectura de streaming

#### Decisión tomada (MVP)

Para avanzar rápido sin perder la base estable de `camara_nodered_mqtt`, se define esta arquitectura:

- **Video:** `HTTP MJPEG` servido por la ESP32-CAM (stream continuo de JPEG multipart).
- **Control y telemetría:** `MQTT` (broker Mosquitto en laptop), reutilizando el patrón del proyecto anterior.
- **Visualización inicial:** navegador en la laptop apuntando al endpoint de stream de la ESP32.

#### Por qué esta opción

- Reutiliza casi todo el firmware actual: inicialización de cámara, WiFi y cliente MQTT.
- Evita sobrecargar MQTT con frames binarios grandes y frecuentes.
- Permite validar streaming con baja complejidad operativa (sin puente RTSP/WebRTC en esta etapa).
- Mantiene la puerta abierta para que YOLO en laptop lea el stream HTTP en la etapa 2.

#### Arquitectura MVP (Fase A cerrada)

1. ESP32-CAM conecta a WiFi (misma lógica del proyecto base).
2. ESP32 publica estado por MQTT (`camara/estado`, `camara/ip`).
3. ESP32 expone endpoint HTTP de stream MJPEG (propuesto: `/stream`).
4. Laptop abre `http://<ip_esp32>/stream` en navegador para ver video en vivo.

#### Interfaces acordadas para la siguiente fase

- **MQTT (sin cambios mayores):**
  - `camara/estado`
  - `camara/ip`
  - (opcional) `camara/control` para comandos de stream on/off o ajuste de calidad.
- **HTTP en ESP32:**
  - `GET /stream` -> flujo MJPEG VGA.
  - (opcional) `GET /snapshot` -> JPEG único para diagnóstico.

### Fase B - Implementación de firmware streaming

- Partir del firmware base de `camara_nodered_mqtt`.
- Reusar inicialización de cámara y conectividad.
- Ajustar parámetros para streaming continuo:
  - `frame_size = VGA`
  - calidad JPEG/fps objetivo
  - manejo de buffers para evitar bloqueos.
- Exponer endpoint/canal de stream según arquitectura elegida.

#### Estado actual (implementado)

Ya se creó la base de firmware en `camara_mqtt_live/firmware`:

- `main/camara_mqtt_live.c`
- `main/CMakeLists.txt`
- `main/Kconfig.projbuild`
- `sdkconfig.defaults`
- `partitions.csv`

El firmware mantiene la base del proyecto anterior y ahora expone:

- `GET /stream` -> stream MJPEG VGA en vivo.
- `GET /snapshot` -> captura JPEG individual.
- `GET /health` -> chequeo simple (`ok`).

Además conserva MQTT para publicar:

- `camara/estado` (online)
- `camara/ip` (IP de la ESP32)

#### Prueba rápida de la Fase B

1. Compilar y flashear:
   ```bash
   cd /home/ernesto/Documentos/Proyectos/ESP32/camara_mqtt_live/firmware
   idf.py set-target esp32
   idf.py menuconfig
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
2. En la laptop abrir:
   - `http://<ip_esp32>/stream`
3. Verificar snapshot:
   - `http://<ip_esp32>/snapshot`

### Fase C - Visualizador en laptop

- Implementar receptor y visualizador de stream.
- Alternativas:
  - navegador (servidor local con stream MJPEG),
  - script Python (`cv2.imshow`) para prueba rápida.
- Agregar métricas mínimas: FPS recibido, reconexión y registro de errores.

#### Estado actual (implementado)

Se implementó un visualizador local en:

- `laptop/stream_viewer.py`
- `laptop/requirements.txt`

Capacidades incluidas:

- Apertura de stream MJPEG de la ESP32 (`/stream`).
- Ventana emergente con OpenCV (`cv2.imshow`).
- Overlay de FPS en tiempo real.
- Reconexion automatica si se cae el stream.
- Guardado de snapshot con tecla `s` en carpeta `snapshots/`.
- Cierre de app con tecla `q`.

#### Como ejecutar el visualizador

En esta laptop se usa el entorno **Anaconda** `sapera_django_yolo26`, que ya incluye **OpenCV** y lo necesario para **YOLO** en las fases siguientes. Activalo antes de ejecutar los scripts de `laptop/`:

```bash
conda activate sapera_django_yolo26
cd /home/ernesto/Documentos/Proyectos/ESP32/camara_mqtt_live
```

Si en otro equipo no tuvieras OpenCV, puedes instalar lo minimo con `pip install -r laptop/requirements.txt` dentro de tu entorno.

1. Ejecutar el visualizador contra la IP de la ESP32:
   ```bash
   python laptop/stream_viewer.py --url http://192.168.100.61/stream
   ```

2. Controles en la ventana:
   - `q`: salir
   - `s`: guardar snapshot

#### Validacion de la fase

- [x] Visualizacion en navegador (`http://<ip_esp32>/` y `/stream`).
- [x] Visualizacion en ventana de escritorio (OpenCV).
- [x] Metrica base de FPS.
- [x] Reconexion automatica ante corte del stream.

### Fase D - Integración YOLO en vivo

- Consumir stream y correr inferencia por frame (o cada N frames).
- Dibujar detecciones en tiempo real.
- Ejecutar en **`sapera_django_yolo26`** (entorno Conda con OpenCV/Ultralytics en esta laptop).
- Ajustar balance precisión/rendimiento:
  - modelo YOLO (nano/small),
  - resolución de inferencia,
  - frecuencia de procesamiento.

#### Estado actual (implementado)

Script **`laptop/yolo_stream_inferencer.py`**:

- Abre el stream MJPEG (`--url`, por defecto debe coincidir con la IP de la ESP32).
- Ejecuta **Ultralytics YOLO** cada `--stride` frames (por defecto 2) para no saturar CPU/GPU.
- Muestra ventana con cajas (`results.plot()`).
- Superpone FPS de video y FPS de inferencia.
- Opcionalmente publica JSON en **`camara/detecciones`** (mismo esquema conceptual que `camara_nodered_mqtt`: `objects_total`, `classes`, `classes_unique`, más `source` y `ts_epoch_ms`).
- **`--no-mqtt`** si solo quieres ver detecciones sin broker.

**Resolución cámara vs YOLO:** la ESP32 entrega **VGA 640×480**. YOLOv8 no exige que el frame sea **640×640**; con `--imgsz 640` (valor por defecto) Ultralytics **reescala y hace letterbox** al tamaño de inferencia. No necesitas cambiar la cámara a cuadrado.

#### Como ejecutar (Fase D)

1. Broker MQTT en la laptop (si usas publicacion), por ejemplo el stack de `camara_nodered_mqtt`:
   ```bash
   cd /home/ernesto/Documentos/Proyectos/ESP32/camara_nodered_mqtt
   docker compose up -d
   ```

2. **Cierra el navegador** si tenias abierto el stream de la ESP32 (un cliente a la vez).

3. Ejecutar inferencia en vivo:
   ```bash
   conda activate sapera_django_yolo26
   cd /home/ernesto/Documentos/Proyectos/ESP32/camara_mqtt_live
   python laptop/yolo_stream_inferencer.py --url http://192.168.100.63/stream
   ```

   Variantes utiles:
   ```bash
   # Mas ligero (inferir 1 de cada 3 frames)
   python laptop/yolo_stream_inferencer.py --url http://<ip_esp32>/stream --stride 3

   # Sin MQTT, solo ventana con detecciones
   python laptop/yolo_stream_inferencer.py --url http://<ip_esp32>/stream --no-mqtt
   ```

4. Suscribirse a detecciones (opcional):
   ```bash
   docker compose exec mosquitto mosquitto_sub -h localhost -t "camara/detecciones" -v
   ```
   (desde el directorio donde tengas Mosquitto en marcha).

### Fase E - Endurecimiento y documentación final

#### Objetivo

Cerrar el ciclo **A→E** con pruebas recomendadas, límites conocidos y una referencia rápida de operación y troubleshooting.

#### Estado (cerrado v1)

- [x] Arquitectura y firmware de streaming validados en hardware (OV3660 / VGA).
- [x] Visualización navegador + `stream_viewer.py`.
- [x] `yolo_stream_inferencer.py` con `--imgsz` por defecto compatible con Ultralytics reciente.
- [x] Documentación de **un solo cliente** en `/stream` y nota VGA vs letterbox YOLO.
- [x] Anexo formal: **`docs/fase-e-cierre.md`**.

#### Protocolo de prueba de estabilidad (recomendado)

1. Una sola pestaña/app consumiendo `http://<ip_esp32>/stream`.
2. Correr **10–15 minutos** `stream_viewer.py` o el navegador: sin reinicios inesperados de la ESP32.
3. Con Mosquitto en marcha, correr `yolo_stream_inferencer.py` el mismo tiempo: revisar FPS en overlay y que MQTT no sature (ajustar `--mqtt-min-interval` si hace falta).
4. Opcional: cortar WiFi unos segundos y comprobar reconexión del script (reintento) y de la ESP32 (monitor serie / `camara/ip`).

#### Afinado rápido (sin recompilar firmware)

| Objetivo | Acción |
|----------|--------|
| Menos carga en laptop | `--stride 3` o más; `--imgsz 320` |
| Menos tráfico WiFi / más FPS en ESP32 | En firmware: subir `jpeg_quality` (número más alto = más compresión) o aumentar el delay entre frames en el handler del stream |
| Solo ver video | Navegador o `stream_viewer.py` |
| Detecciones + MQTT | `yolo_stream_inferencer.py` (broker en `127.0.0.1:1883` o `--mqtt-host`) |

#### Troubleshooting consolidado

| Síntoma | Causa probable | Qué hacer |
|---------|----------------|-----------|
| `404` o "Nothing matches URI" en `http://ip/` | Firmware antiguo sin `GET /` | Actualizar firmware; o usar `/stream` directamente |
| OpenCV timeout ~30 s al abrir stream | Otra pestaña con `/stream` abierta | Cerrar navegador u otra app que use el mismo stream |
| `TypeError imgsz=None` (YOLO) | Versión Ultralytics estricta | Usar repo actual: `--imgsz` por defecto 640 |
| `DeprecationWarning` MQTT Callback API | Paho 2.x | Repo actual usa API v2 cuando está disponible |
| Aviso Wayland / Gnome | OpenCV/Qt | Inofensivo; opcional: `export QT_QPA_PLATFORM=wayland` |
| `FB-OVF` en monitor (poco frecuente) | Buffers cámara vs consumo | Firmware usa `GRAB_WHEN_EMPTY` y `fb_count=1`; evitar muchos clientes |

#### Límites conocidos (v1)

- **Concurrencia:** la ESP32-CAM no escala como un servidor de video profesional; tratar como **un cliente de stream a la vez** para uso fiable.
- **Rendimiento YOLO:** limitado por CPU/GPU de la laptop; el FPS del overlay "infer" puede ser menor que el FPS del video MJPEG.
- **Seguridad:** HTTP sin TLS en LAN; no exponer el puerto 80 de la ESP32 a Internet sin reverse proxy y autenticación.

## Supuestos y decisiones iniciales

- La laptop y la ESP32 estarán en la misma red local.
- Se mantiene MQTT para señalización/estado, incluso si el video viaja por otro canal.
- El primer objetivo es **funcionalidad estable**, luego optimización de latencia y calidad.
- Scripts de laptop (visualizador, YOLO en vivo): usar **`conda activate sapera_django_yolo26`** (OpenCV + Ultralytics/YOLO ya disponibles en tu entorno).

## Riesgos técnicos a vigilar

- Capacidad real de FPS de ESP32-CAM en VGA con WiFi activo.
- Saturación de red o de CPU en laptop durante inferencia.
- Aumento de latencia por serialización JPEG y transporte.
- Cortes de WiFi en sesiones largas.

## Historial del plan (A–E) — estado

| # | Entrega | Estado |
|---|---------|--------|
| A | Arquitectura MJPEG + MQTT estado | Hecho |
| B | Firmware `/`, `/stream`, `/snapshot`, `/health` | Hecho |
| C | `stream_viewer.py` | Hecho |
| D | `yolo_stream_inferencer.py` + MQTT detecciones | Hecho |
| E | Documentación de cierre y límites | Hecho (`README` + `docs/fase-e-cierre.md`) |

## Próximas mejoras (fuera de v1)

Ideas opcionales: proxy en laptop para varios visores, control MQTT del stream, calidad JPEG dinámica, otro modelo YOLO o despliegue con GPU dedicada, hardening de red.

---

**Proyecto MVP v1:** cerrado a nivel documentación y alcance acordado. Para reproducir: seguir secciones Fase B (firmware), Fase C (visualizador) y Fase D (YOLO); detalles de cierre en **`docs/fase-e-cierre.md`**.
