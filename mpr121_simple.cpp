#include <iostream>
#include <iomanip>
#include <csignal>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

const char *i2cDevice = "/dev/i2c-1";
const int mpr121Address = 0x5A;
volatile bool keepRunning = true;

void signalHandler(int) {
    keepRunning = false;
}

bool writeRegister(int file, uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    return write(file, buffer, 2) == 2;
}

bool readRegisters(int file, uint8_t startReg, uint8_t *buffer, size_t length) {
    if (write(file, &startReg, 1) != 1) return false;
    return read(file, buffer, length) == (ssize_t)length;
}

int main() {
    std::signal(SIGINT, signalHandler);

    int file = open(i2cDevice, O_RDWR);
    if (file < 0 || ioctl(file, I2C_SLAVE, mpr121Address) < 0) {
        std::cerr << "I2C setup failed\n";
        return 1;
    }

    // Stop electrode scanning before config
    writeRegister(file, 0x2B, 0x00);

    // Set touch/release thresholds
    for (int i = 0; i < 12; ++i) {
        writeRegister(file, 0x41 + i * 2, 12);
        writeRegister(file, 0x42 + i * 2, 6);
    }

    // Enable all 12 electrodes
    writeRegister(file, 0x2B, 0x0C);

    std::cout << "Reading raw electrode data. Press Ctrl-C to exit.\n";

    while (keepRunning) {
        uint8_t rawData[24];
        if (!readRegisters(file, 0x04, rawData, sizeof(rawData))) {
            std::cerr << "Failed to read raw data\n";
            break;
        }

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::cout << std::put_time(std::localtime(&t), "%H:%M:%S") << " | ";

        for (int i = 0; i < 12; ++i) {
            uint16_t value = rawData[i * 2] | (rawData[i * 2 + 1] << 8);
            std::cout << std::setw(4) << value << " ";
        }
        std::cout << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "Exiting.\n";
    close(file);
    return 0;
}