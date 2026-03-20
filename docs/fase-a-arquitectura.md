# Fase A - Decision de arquitectura (MVP)

## Objetivo de la fase

Definir la arquitectura de streaming para iniciar rapido, reutilizando al maximo la base funcional de `camara_nodered_mqtt`.

## Opciones evaluadas

1. **HTTP MJPEG desde ESP32 (elegida)**
2. JPEG por MQTT (frames continuos)
3. RTSP/WebRTC con puente en laptop

## Decision

Se elige **HTTP MJPEG desde ESP32** para el canal de video y se mantiene **MQTT** para control/estado.

## Justificacion tecnica

- Requiere menos cambios sobre el firmware base (camara, WiFi y MQTT ya funcionan).
- Evita usar MQTT como transporte de video continuo, lo cual incrementaria carga y latencia.
- Permite una validacion rapida desde navegador: `http://<ip_esp32>/stream`.
- Facilita la etapa 2, donde la laptop podra consumir el stream para YOLO.

## Arquitectura acordada

- **ESP32-CAM**
  - Captura en `VGA (640x480)`.
  - Publica `camara/estado` y `camara/ip` por MQTT.
  - Expone endpoint HTTP `GET /stream` (MJPEG).
- **Laptop**
  - Mantiene Mosquitto para mensajeria.
  - Visualiza stream en navegador.
  - En etapa 2, procesa stream con YOLO.

## Compatibilidad con el proyecto base

Se reutiliza de `camara_nodered_mqtt`:

- Pinout y configuracion de camara.
- Inicializacion de WiFi y reconexion.
- Cliente MQTT y topicos de estado.

Se reemplaza:

- Flujo de captura por disparo + `HTTP POST /recibir-foto`

Por:

- Captura continua + servidor HTTP con stream MJPEG.

## Definicion de listo para cerrar Fase A

- [x] Arquitectura seleccionada y documentada.
- [x] Interfaces minimas definidas (`/stream`, `camara/estado`, `camara/ip`).
- [x] Criterio de continuidad hacia Fase B establecido (reuso del firmware base).
