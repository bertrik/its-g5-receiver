# its-g5-receiver
Simple ITS-G5 receiver for ITS-G5 802.11p frames


## Compiling the sniffer firmware

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

## Uploading
Make sure the virtual environment is activated (see above).

Compile and upload:
```bash
pio run -t upload
```