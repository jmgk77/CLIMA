# CLIMA Weather Station

A compact weather monitoring station built on ESP8266 (WeMos D1 Mini) using an SHT30 temperature and humidity sensor. Features a web interface for real-time data visualization, historical graphs, MQTT support, and over-the-air updates.

## Features

- **Real-time Monitoring**: Displays current temperature and humidity readings.
- **Historical Data**: Stores and visualizes daily and monthly temperature/humidity graphs.
- **Web Interface**: Responsive web server with Chart.js for data visualization.
- **MQTT Integration**: Publishes sensor data to MQTT topics for integration with home automation systems.
- **WiFi Management**: Built-in WiFiManager for easy network configuration.
- **Discovery Protocols**: Supports SSDP, LLMNR, mDNS, and NetBIOS for device discovery.
- **File Server**: Serves CSV data files and supports firmware updates.
- **OTA Updates**: Compressed over-the-air firmware updates.
- **Data Persistence**: Partial data survives reboots using EEPROM and SPIFFS.

## Hardware Requirements

- WeMos D1 Mini (ESP8266-based board)
- SHT30 temperature and humidity sensor (I2C address 0x44)
- Power supply (5V recommended)

## Software Requirements

- PlatformIO (for building and uploading)
- Python 3 (for build scripts)
- GCC (for building cache tools)

## Installation

1. Clone or download the repository.
2. Install PlatformIO if not already installed.
3. Connect the hardware as per the schematic (SHT30 to I2C pins).
4. Open the project in PlatformIO.
5. Build and upload the firmware.

## Building

Use PlatformIO to build the project:

```bash
platformio run
```

Upload to the board:

```bash
platformio run --target upload
```

The build scripts automatically increment the version number and compress the firmware for OTA.

## Usage

1. Power on the device.
2. Use WiFiManager to connect to your network (access point "CLIMA" will appear).
3. Access the web interface at the device's IP address.
4. View real-time data, graphs, and configuration options.
5. Configure MQTT settings if desired.

### Web Interface

- **Main Page**: Current readings and graphs (today/month).
- **Config Page**: System info, version, and controls (update, reboot, reset).
- **Files Page**: Access stored CSV data files.

### MQTT Topics

- `CLIMA/DESCRIPTION`: Device description
- `CLIMA/IP`: Local IP address
- `CLIMA/TEMPERATURE`: Current temperature
- `CLIMA/HUMIDITY`: Current humidity

## Configuration

- MQTT settings are stored in EEPROM.
- Data is saved to SPIFFS in CSV format.
- Build versioning is handled automatically.

## Tools

- `build.sh`: Compiles cache management tools.
- `dump_cache.c`: Tool to dump cache data.
- `trim_cache.c`: Tool to trim cache data.

## Models

FreeCAD models for the physical enclosure:
- `stevenson_screen.FCStd`: Stevenson screen design.
- `weather_station.FCStd`: Weather station housing.

## Contributing

Contributions are welcome. Please ensure code follows the existing style and test thoroughly.

## License

[Specify license if applicable]

## Version

Current version: v1.0.159 (2026-05-07)</content>
<parameter name="filePath">/home/titan/Dropbox/WORK/CLIMA/README.md