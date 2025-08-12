#!/bin/bash

echo "Setting up Raspberry Pi for MPR121 sensors..."

# Check if running as root
if [ "$EUID" -eq 0 ]; then 
    echo "Please don't run this script as root"
    exit 1
fi

# Enable I2C in raspi-config
echo "Enabling I2C interface..."
sudo raspi-config nonint do_i2c 0

# Install required packages
echo "Installing required packages..."
sudo apt update
sudo apt install -y i2c-tools build-essential git

# Add user to i2c group
echo "Adding user to i2c group..."
sudo usermod -a -G i2c $USER

# Check if I2C modules are loaded
echo "Checking I2C modules..."
if ! lsmod | grep -q i2c_dev; then
    echo "Loading I2C modules..."
    sudo modprobe i2c-dev
    
    # Add to modules for automatic loading at boot
    if ! grep -q "i2c-dev" /etc/modules; then
        echo "i2c-dev" | sudo tee -a /etc/modules
    fi
fi

# Set I2C speed (optional - default is usually fine)
echo "Configuring I2C speed..."
if ! grep -q "dtparam=i2c_arm_baudrate" /boot/config.txt; then
    echo "dtparam=i2c_arm_baudrate=100000" | sudo tee -a /boot/config.txt
fi

# Create udev rule for I2C permissions (alternative to adding to group)
echo "Creating udev rule for I2C access..."
echo 'SUBSYSTEM=="i2c-dev", GROUP="i2c", MODE="0666"' | sudo tee /etc/udev/rules.d/90-i2c.rules

echo "Setup complete!"
echo ""
echo "IMPORTANT: Please reboot your Raspberry Pi for all changes to take effect:"
echo "sudo reboot"
echo ""
echo "After reboot, you can:"
echo "1. Test I2C with: i2cdetect -y 1"
echo "2. Build the forwarder with: make"
echo "3. Run the forwarder with: ./mpr121_forwarder"
echo ""
echo "Expected I2C addresses for MPR121 sensors:"
echo "- Sensor 0: 0x5a (default)"
echo "- Sensor 1: 0x5b (ADDR tied to VCC)"