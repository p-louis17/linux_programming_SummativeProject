from setuptools import setup, Extension

sensor_module = Extension(
    "sensor_processor",
    sources=["sensor_processor.c"],
)

setup(
    name="sensor_processor",
    version="1.0",
    description="C extension for IoT sensor data filtering and analysis",
    ext_modules=[sensor_module],
)