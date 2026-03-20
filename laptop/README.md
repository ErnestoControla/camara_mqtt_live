# Scripts en laptop (OpenCV / YOLO)

En esta máquina se usa el entorno Conda **`sapera_django_yolo26`**, que ya incluye OpenCV y las dependencias para YOLO.

```bash
conda activate sapera_django_yolo26
cd /home/ernesto/Documentos/Proyectos/ESP32/camara_mqtt_live
```

- `stream_viewer.py` — visualizador MJPEG (Fase C).
- `requirements.txt` — referencia mínima (`opencv-python`) por si hace falta en otro equipo; en `sapera_django_yolo26` normalmente no hace falta instalar nada extra.

**Nota:** no abras el stream en el navegador y en OpenCV a la vez; la ESP32 puede no atender bien varios clientes y OpenCV mostrará timeout hasta que cierres la otra pestaña.
