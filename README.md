# MPR121 Data Forwarder for Raspberry Pi

A Linux port of Arduino MPR121 touch sensor data collection and forwarding system. This application reads data from two MPR121 capacitive touch sensors via I2C and forwards the data to a remote dataserver over TCP/IP with automatic reconnection capabilities.

## Features

- **Dual MPR121 Support**: Reads from two MPR121 sensors (addresses 0x5A and 0x5B)
- **Precise Timing**: Uses Linux timerfd for accurate 20ms sampling intervals
- **Robust Networking**: Automatic reconnection with configurable delay
- **Signal Handling**: Graceful shutdown on SIGINT/SIGTERM
- **Command Line Configuration**: Configurable host and port via command line arguments
- **Systemd Integration**: Ready for deployment as a system service

## Hardware Requirements

- Raspberry Pi (any model with I2C support)
- 2x MPR121 Capacitive Touch Sensor Breakouts
- I2C connections between Pi and sensors

### Wiring

| MPR121 Pin | Raspberry Pi Pin | Notes |
|------------|------------------|-------|
| VCC        | 3.3V (Pin 1)     | Power supply |
| GND        | GND (Pin 6)      | Ground |
| SCL        | SCL (Pin 5)      | I2C Clock |
| SDA        | SDA (Pin 3)      | I2C Data |
| ADDR       | 3.3V (sensor 1 only) | Sets address to 0x5B |

- **Sensor 0**: ADDR pin floating or to GND (address 0x5A)
- **Sensor 1**: ADDR pin to 3.3V (address 0x5B)

## Quick Start

### 1. Hardware Setup

Connect your MPR121 sensors to the Raspberry Pi I2C bus as described in the wiring section above.

### 2. I2C Configuration

Run the provided setup script to configure I2C on your Raspberry Pi:

```bash
chmod +x setup_i2c.sh
./setup_i2c.sh
sudo reboot
```

### 3. Verify I2C Connection

After reboot, verify your sensors are detected:

```bash
i2cdetect -y 1
```

You should see devices at addresses `0x5a` and `0x5b`:

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- 5a 5b -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --
```

### 4. Build and Run

```bash
# Build the application
make

# Run with default settings (connects to 192.168.88.40:4620)
./mpr121_forwarder

# Run with custom host and port
./mpr121_forwarder -h 192.168.1.100 -p 4620

# Run with different sampling rate (10ms = 100Hz)
./mpr121_forwarder -t 10

# Combine options
./mpr121_forwarder -h server.local -p 5000 -t 50
```

## Command Line Options

```
Usage: mpr121_forwarder [OPTIONS]
Options:
  -h, --host <address>    Dataserver host address (default: 192.168.88.40)
  -p, --port <port>       Dataserver port (default: 4620)
  -t, --timer <ms>        Timer interval in milliseconds (default: 20)
  --help                  Show this help message

Example:
  mpr121_forwarder -h 192.168.1.100 -p 4620 -t 50
  mpr121_forwarder --host server.local --timer 10  # 100Hz sampling
```

## Installation as System Service

### 1. Install Binary

```bash
make install
```

This installs the binary to `/usr/local/bin/mpr121_forwarder`.

### 2. Create and Install Service

```bash
# Generate systemd service file
make service

# Install the service
sudo cp mpr121-forwarder.service /etc/systemd/system/

# Reload systemd and enable the service
sudo systemctl daemon-reload
sudo systemctl enable mpr121-forwarder.service

# Start the service
sudo systemctl start mpr121-forwarder.service
```

### 3. Service Management

```bash
# Check service status
sudo systemctl status mpr121-forwarder.service

# View logs
sudo journalctl -u mpr121-forwarder.service -f

# Stop service
sudo systemctl stop mpr121-forwarder.service

# Restart service
sudo systemctl restart mpr121-forwarder.service
```

## Configuration

### Network & Timing Settings

- **Default Host**: `192.168.88.40`
- **Default Port**: `4620`
- **Default Sample Rate**: `50Hz` (20ms interval)
- **Reconnection Delay**: `5000ms` (5 seconds)

Command line arguments override the compiled-in defaults:

```bash
# High-speed sampling at 200Hz (5ms interval)
./mpr121_forwarder -t 5

# Low-speed sampling at 10Hz (100ms interval) 
./mpr121_forwarder -t 100

# Custom server with 25Hz sampling
./mpr121_forwarder -h 10.0.1.50 -p 8080 -t 40
```

To change the reconnection delay, modify the constant in `mpr121_forwarder.cpp`:

```cpp
const int RECONNECT_DELAY_MS = 5000;  // 5 second delay
```

## Data Format

The forwarder sends data to the dataserver using the same binary protocol as the original Arduino version:

### Data Points Sent

| Variable Name | Type | Description |
|---------------|------|-------------|
| `grasp/sensor0/touched` | DSERV_SHORT | Touch status bitmask for sensor 0 |
| `grasp/sensor0/vals` | DSERV_SHORT[6] | Filtered capacitance values for sensor 0 |
| `grasp/sensor1/touched` | DSERV_SHORT | Touch status bitmask for sensor 1 |
| `grasp/sensor1/vals` | DSERV_SHORT[6] | Filtered capacitance values for sensor 1 |

### Protocol Details

- Uses binary message format with `'>'` prefix
- Fixed 128-byte message length
- Includes timestamp, variable name, data type, and payload
- Compatible with existing dataserver infrastructure

## Troubleshooting

### I2C Issues

**Problem**: `i2cdetect -y 1` shows no devices

**Solutions**:
1. Check wiring connections
2. Ensure I2C is enabled: `sudo raspi-config` → Interface Options → I2C → Enable
3. Verify I2C modules are loaded: `lsmod | grep i2c`
4. Try bus 0: `i2cdetect -y 0` (older Pi models)

**Problem**: Permission denied accessing `/dev/i2c-1`

**Solutions**:
1. Add user to i2c group: `sudo usermod -a -G i2c $USER` then logout/login
2. Check udev rules: Ensure `/etc/udev/rules.d/90-i2c.rules` exists

### Network Issues

**Problem**: "Connection failed" or "Connection timeout"

**Solutions**:
1. Verify dataserver is running and accessible
2. Check firewall settings on both Pi and server
3. Test connectivity: `telnet <server_address> <port>`
4. Verify network configuration

**Problem**: Connection drops frequently

**Solutions**:
1. Check network stability
2. Verify dataserver can handle the connection load
3. Adjust `RECONNECT_DELAY_MS` if needed

### Build Issues

**Problem**: "fatal error: linux/i2c-dev.h: No such file or directory"

**Solution**: Install development packages:
```bash
sudo apt update
sudo apt install libi2c-dev i2c-tools
```

**Problem**: Compiler not found

**Solution**: Install build tools:
```bash
sudo apt install build-essential
```

## Development

### Build Options

```bash
# Standard build
make

# Debug build (with debug symbols and DEBUG flag)
make debug

# Clean build artifacts
make clean
```

### Project Structure

```
.
├── mpr121_forwarder.cpp    # Main application source
├── Makefile                # Build configuration
├── setup_i2c.sh           # I2C setup script
├── README.md               # This file
└── mpr121-forwarder.service # Systemd service file (generated)
```

## License

This project maintains compatibility with the original dataserver infrastructure while providing a robust Linux implementation for Raspberry Pi deployment.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly on hardware
5. Submit a pull request

## Support

For issues and questions:
1. Check the troubleshooting section above
2. Verify hardware connections and I2C configuration
3. Check system logs: `sudo journalctl -u mpr121-forwarder.service`
4. Open an issue with detailed error information and system configuration