#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

const char *i2cDevice = "/dev/i2c-1";
const int mpr121Address = 0x5A;

bool writeRegister(int file, uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    return write(file, buffer, 2) == 2;
}

bool readRegisters(int file, uint8_t startReg, uint8_t *buffer, size_t length) {
    if (write(file, &startReg, 1) != 1) return false;
    return read(file, buffer, length) == (ssize_t)length;
}

int main() {
    int file = open(i2cDevice, O_RDWR);
    if (file < 0 || ioctl(file, I2C_SLAVE, mpr121Address) < 0) {
        std::cerr << "I2C setup failed\n";
        return 1;
    }

    // Stop electrode scanning before config
    writeRegister(file, 0x2B, 0x00);

    // Set touch/release thresholds for all 12 electrodes
    for (int i = 0; i < 12; ++i) {
        writeRegister(file, 0x41 + i * 2, 12); // Touch threshold
        writeRegister(file, 0x42 + i * 2, 6);  // Release threshold
    }

    // Enable all 12 electrodes (0x0C = 12)
    writeRegister(file, 0x2B, 0x0C);

    // Read raw data (0x04–0x1D, 2 bytes per electrode)
    uint8_t rawData[24];
    if (!readRegisters(file, 0x04, rawData, sizeof(rawData))) {
        std::cerr << "Failed to read raw data\n";
        close(file);
        return 1;
    }

    for (int i = 0; i < 12; ++i) {
        uint16_t value = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
        std::cout << "Electrode " << i << ": " << value << "\n";
    }

    close(file);
    return 0;
}