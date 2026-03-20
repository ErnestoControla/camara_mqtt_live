# camara_mqtt_live

Proyecto base para evolucionar desde `camara_nodered_mqtt` hacia **video en streaming** con ESP32-CAM, manteniendo la integración de red/mensajería y preparando la segunda etapa de inferencia con YOLO en la laptop.

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

### Fase D - Integración YOLO en vivo

- Consumir stream y correr inferencia por frame (o cada N frames).
- Dibujar detecciones en tiempo real.
- Ajustar balance precisión/rendimiento:
  - modelo YOLO (nano/small),
  - resolución de inferencia,
  - frecuencia de procesamiento.

### Fase E - Endurecimiento y documentación final

- Pruebas de estabilidad (duración, reconexión WiFi, reinicio de servicios).
- Ajustes de rendimiento.
- Documentar arquitectura final, comandos y troubleshooting.

## Supuestos y decisiones iniciales

- La laptop y la ESP32 estarán en la misma red local.
- Se mantiene MQTT para señalización/estado, incluso si el video viaja por otro canal.
- El primer objetivo es **funcionalidad estable**, luego optimización de latencia y calidad.

## Riesgos técnicos a vigilar

- Capacidad real de FPS de ESP32-CAM en VGA con WiFi activo.
- Saturación de red o de CPU en laptop durante inferencia.
- Aumento de latencia por serialización JPEG y transporte.
- Cortes de WiFi en sesiones largas.

## Backlog inicial (orden sugerido)

1. Definir arquitectura de streaming para MVP.
2. Crear firmware mínimo que emita stream VGA continuo.
3. Confirmar visualización en laptop (navegador o ventana).
4. Medir rendimiento base (FPS/latencia/estabilidad).
5. Integrar YOLO sobre stream.
6. Publicar resultados y métricas por MQTT.

## Próximo paso

Con la Fase A definida, el siguiente paso es implementar la **Fase B**: clonar el firmware base de `camara_nodered_mqtt`, mantener su configuración de cámara/red y reemplazar el envío por POST por un servidor HTTP con endpoint `/stream`.
