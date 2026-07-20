# its-g5-receiver
Simple ITS-G5 sniffer for ITS-G5 802.11p frames.

This was inspired by https://git.devlol.org/jstsmthrgk/simple-its-g5-receiver-firmware with the following differences:
* More Arduino-like, using fewer IDF/FreeRTOS-specific features
* Uses a simpler, fixed size, ringbuffer instead of dynamically allocating/freeing buffers

## Compiling sniffer firmware

- If you don't already have a Python virtual environment, create one:

  ```bash
  python -m venv .venv
  ```

- Activate the virtual environment:

  ```bash
  source .venv/bin/activate
  ```

- Install PlatformIO in the virtual environment:

  ```bash
  pip install platformio
  ```

## Programming firmware
Make sure the virtual environment is activated (see above).

Compile and upload the firmware:
```bash
pio run -t upload
```

## Configuration

There is not anything to configure.
A simple command line processor runs on the USB port (likely /dev/ttyACM0 on Linux)

## Use

Provide power to the board using the "USB" port on the esp32-c5.
It can also be powered through the 5V/GND pins, e.g. when connected to an esp32-c3 bridge.

The LED flashes blinks every second to indicate that the sniffer is active and waiting for packets.
The LED flashes green when a packet was received and it is being forwarded over the serial port.
