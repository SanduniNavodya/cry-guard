from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse
import logging

from services.state_manager import state_manager

router = APIRouter(prefix="/api", tags=["Sensors"])
logger = logging.getLogger(__name__)


@router.post("/sensor-data", summary="Receive sensor data from ESP32")
async def receive_sensor_data(request: Request):
    """
    Receives JSON sensor data from ESP32:
    {
        "temperature": 28.5,
        "humidity": 65.0,
        "motion": true,
        "light_dark": false
    }
    """
    try:
        data = await request.json()
        logger.info(f"Sensor data received: {data}")

        await state_manager.update_sensor_data(data)

        return JSONResponse(content={"status": "ok", "message": "Sensor data received."})

    except Exception as e:
        logger.error(f"Error processing sensor data: {str(e)}")
        return JSONResponse(
            status_code=400,
            content={"status": "error", "message": str(e)},
        )


@router.get("/sensor-data", summary="Get latest sensor data")
async def get_sensor_data():
    """Return the latest sensor readings."""
    return JSONResponse(content={
        "esp_connected": state_manager.is_esp_connected(),
        "sensor_data": state_manager.sensor_data,
    })


@router.get("/status", summary="Get system status")
async def get_status():
    """Return full system status (ESP32 connection, sensors, cry status)."""
    return JSONResponse(content={
        "esp_connected": state_manager.is_esp_connected(),
        "sensor_data": state_manager.sensor_data,
        "cry_status": state_manager.cry_status,
        "notifications": state_manager.notifications[-10:],
    })
