# Sensor-PoolTemp
Arduino-based Pool temperature sensor that uses MQTT to publish the readings every five minutes.

This uses a water-proof DS18B20 temperature sensor to sense the current temperature of the pool, and sends this out via WiFi and the MQTT protocol every five minutes.  The sensor + Adafruit Feather M0 + ATWINC1500 is powered by a small solar panel with batteries to power it during the night.

