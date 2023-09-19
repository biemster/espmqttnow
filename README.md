# espmqttnow
Bridge between ESPNOW and MQTT

Basically combining the following two projects
- https://github.com/thomasfla/Linux-ESPNOW
- https://github.com/LiamBindle/MQTT-C

trying to get it running on the QCA9533 found in those $5 repeaters:

![image](https://github.com/biemster/espmqttnow/assets/5699190/bced402e-a19d-4ad9-98dd-678474d90344)

It's made for the OpenWrt build here: https://github.com/biemster/QCA953X_OpenWrt
but it should compile/run basically on any Linux if you replace the toolchain in the Makefile.

There are some (micro)python scripts present as well to help with debugging:
The `micropython` folder contains 2 Micropython scripts that should be run on an ESP32
with a very recent version of MicroPython on it (1.20 which is the latest as of this writing
does not include the espnow package, so take a nighly build).
The `mqtt_test.py` script simply connects to the MQTT broker you specify and subscribes to the `espnow` topic.