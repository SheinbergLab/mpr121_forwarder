#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

// I2C includes for Linux
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define DPOINT_BINARY_MSG_CHAR '>'
#define DPOINT_BINARY_FIXED_LENGTH 128
#define DEFAULT_TIMER_INTERVAL_MS 20
#define NSENSORS 6

// MPR121 Constants
#define MPR121_I2CADDR_DEFAULT 0x5A
#define MPR121_TOUCHSTATUS_L 0x00
#define MPR121_TOUCHSTATUS_H 0x01
#define MPR121_FILTDATA_0L 0x04
#define MPR121_CONFIG2 0x5D
#define MPR121_ECR 0x5E

// Dataserver configuration
#define DSERV_PORT 4620
const char* DEFAULT_DATASERVER_ADDRESS = "192.168.88.40";
const int RECONNECT_DELAY_MS = 5000;

typedef enum {
    DSERV_BYTE = 0,
    DSERV_STRING,
    DSERV_FLOAT,
    DSERV_DOUBLE,
    DSERV_SHORT,
    DSERV_INT,
    DSERV_DG,
    DSERV_SCRIPT,
    DSERV_TRIGGER_SCRIPT,
    DSERV_EVT,
    DSERV_NONE,
    DSERV_UNKNOWN,
} ds_datatype_t;

class MPR121 {
private:
    int i2c_fd;
    uint8_t i2c_addr;
    
public:
    MPR121(uint8_t addr = MPR121_I2CADDR_DEFAULT) : i2c_fd(-1), i2c_addr(addr) {}
    
    ~MPR121() {
        if (i2c_fd >= 0) {
            close(i2c_fd);
        }
    }
    
    bool begin(const char* i2c_device = "/dev/i2c-1") {
		// Open I2C device
		i2c_fd = open(i2c_device, O_RDWR);
		if (i2c_fd < 0) {
			std::cerr << "Failed to open I2C device: " << i2c_device << std::endl;
			return false;
		}
		
		// Set I2C slave address
		if (ioctl(i2c_fd, I2C_SLAVE, i2c_addr) < 0) {
			std::cerr << "Failed to set I2C slave address: 0x" << std::hex << i2c_addr << std::endl;
			close(i2c_fd);
			i2c_fd = -1;
			return false;
		}
	
		// Stop electrode scanning before config (same as your working script)
		writeRegister(0x5E, 0x00);  // ECR register
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	
		// Set touch/release thresholds for first 6 electrodes (same as your working script)
		for (int i = 0; i < 6; ++i) {
			writeRegister(0x41 + i * 2, 12);  // Touch threshold
			writeRegister(0x42 + i * 2, 6);   // Release threshold
		}
	
		// Enable first 6 electrodes (same as your working script, but only 6 instead of 12)
		writeRegister(0x5E, 0x06);  // Enable 6 electrodes: 0x06 = 0b00000110
		
		// Settle time
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		
		// Debug output
		uint8_t ecr_val = readRegister8(0x5E);
		std::cout << "ECR register: 0x" << std::hex << (int)ecr_val << std::endl;
		
		return true;
    }
    
    uint16_t touched() {
        uint8_t t = readRegister8(MPR121_TOUCHSTATUS_L);
        uint16_t v = readRegister8(MPR121_TOUCHSTATUS_H);
        return ((v << 8) | t);
    }
    
    uint16_t filteredData(uint8_t t) {
        if (t > 12) return 0;
        return readRegister16(MPR121_FILTDATA_0L + t * 2);
    }
    
private:
    void writeRegister(uint8_t reg, uint8_t value) {
        uint8_t buffer[2] = {reg, value};
        if (write(i2c_fd, buffer, 2) != 2) {
            std::cerr << "Failed to write to register 0x" << std::hex << (int)reg << std::endl;
        }
    }
    
    uint8_t readRegister8(uint8_t reg) {
        if (write(i2c_fd, &reg, 1) != 1) {
            std::cerr << "Failed to write register address" << std::endl;
            return 0;
        }
        
        uint8_t value;
        if (read(i2c_fd, &value, 1) != 1) {
            std::cerr << "Failed to read register" << std::endl;
            return 0;
        }
        
        return value;
    }
    
    uint16_t readRegister16(uint8_t reg) {
        if (write(i2c_fd, &reg, 1) != 1) {
            std::cerr << "Failed to write register address" << std::endl;
            return 0;
        }
        
        uint8_t buffer[2];
        if (read(i2c_fd, buffer, 2) != 2) {
            std::cerr << "Failed to read register" << std::endl;
            return 0;
        }
        
        return (buffer[1] << 8) | buffer[0];
    }
};

class DataserverClient {
private:
    int sockfd;
    std::string server_address;
    int server_port;
    std::atomic<bool> connected;
    std::atomic<bool> should_reconnect;
    
public:
    DataserverClient(const std::string& addr, int port) 
        : sockfd(-1), server_address(addr), server_port(port), 
          connected(false), should_reconnect(true) {}
    
    ~DataserverClient() {
        disconnect();
    }
    
    bool connect() {
        if (connected.load()) {
            return true;
        }
        
        // Create socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Set socket options
        int flag = 1;
        setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        
        // Set non-blocking for connection timeout
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
        
        // Resolve hostname
        struct hostent* host_entry = gethostbyname(server_address.c_str());
        if (!host_entry) {
            std::cerr << "Failed to resolve hostname: " << server_address << std::endl;
            close(sockfd);
            sockfd = -1;
            return false;
        }
        
        // Setup server address
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        memcpy(&server_addr.sin_addr, host_entry->h_addr, host_entry->h_length);
        
        // Attempt connection
        int result = ::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        if (result < 0) {
            if (errno == EINPROGRESS) {
                // Wait for connection to complete
                fd_set write_fds;
                struct timeval timeout = {5, 0}; // 5 second timeout
                
                FD_ZERO(&write_fds);
                FD_SET(sockfd, &write_fds);
                
                result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
                
                if (result > 0) {
                    int so_error;
                    socklen_t len = sizeof(so_error);
                    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                    
                    if (so_error != 0) {
                        std::cerr << "Connection failed: " << strerror(so_error) << std::endl;
                        close(sockfd);
                        sockfd = -1;
                        return false;
                    }
                } else {
                    std::cerr << "Connection timeout" << std::endl;
                    close(sockfd);
                    sockfd = -1;
                    return false;
                }
            } else {
                std::cerr << "Connection failed: " << strerror(errno) << std::endl;
                close(sockfd);
                sockfd = -1;
                return false;
            }
        }
        
        // Set back to blocking mode
        fcntl(sockfd, F_SETFL, flags & ~O_NONBLOCK);
        
        connected.store(true);
        std::cout << "Connected to dataserver at " << server_address << ":" << server_port << std::endl;
        return true;
    }
    
    void disconnect() {
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }
        connected.store(false);
    }
    
    bool isConnected() const {
        return connected.load();
    }
    
    bool testConnection() {
        if (!connected.load() || sockfd < 0) {
            return false;
        }
        
        // Use recv with MSG_PEEK and MSG_DONTWAIT to test if socket is still alive
        char test_buf[1];
        int result = recv(sockfd, test_buf, 1, MSG_PEEK | MSG_DONTWAIT);
        
        if (result == 0) {
            // Connection closed by remote host
            std::cerr << "Connection closed by remote host" << std::endl;
            connected.store(false);
            disconnect();
            return false;
        } else if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, but connection is still alive
                return true;
            } else {
                // Real error occurred
                std::cerr << "Connection test failed: " << strerror(errno) << std::endl;
                connected.store(false);
                disconnect();
                return false;
            }
        }
        
        return true; // Connection is good
    }
    
    bool writeToDataserver(const char* varname, int dtype, int len, void* data) {
        if (!connected.load()) {
            return false;
        }
        
        static char buf[DPOINT_BINARY_FIXED_LENGTH];
        uint8_t cmd = DPOINT_BINARY_MSG_CHAR;
        uint16_t varlen = strlen(varname);
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        uint32_t datatype = dtype;
        uint32_t datalen = len;
        
        uint16_t bufidx = 0;
        uint16_t total_bytes = sizeof(uint8_t) + sizeof(uint16_t) + varlen + 
                              sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) + len;
        
        if (total_bytes > sizeof(buf)) {
            std::cerr << "Data too large for buffer" << std::endl;
            return false;
        }
        
        // Pack the data
        memcpy(&buf[bufidx], &cmd, sizeof(uint8_t));
        bufidx += sizeof(uint8_t);
        
        memcpy(&buf[bufidx], &varlen, sizeof(uint16_t));
        bufidx += sizeof(uint16_t);
        
        memcpy(&buf[bufidx], varname, varlen);
        bufidx += varlen;
        
        memcpy(&buf[bufidx], &timestamp, sizeof(uint64_t));
        bufidx += sizeof(uint64_t);
        
        memcpy(&buf[bufidx], &datatype, sizeof(uint32_t));
        bufidx += sizeof(uint32_t);
        
        memcpy(&buf[bufidx], &datalen, sizeof(uint32_t));
        bufidx += sizeof(uint32_t);
        
        memcpy(&buf[bufidx], data, datalen);
        bufidx += datalen;
        
        // Send the data
        ssize_t bytes_sent = send(sockfd, buf, sizeof(buf), MSG_NOSIGNAL);
        if (bytes_sent < 0) {
            if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
                std::cerr << "Connection lost, will attempt reconnection" << std::endl;
                connected.store(false);
                disconnect();
            } else {
                std::cerr << "Send failed: " << strerror(errno) << std::endl;
            }
            return false;
        }
        
        return true;
    }
    
    void startReconnectLoop() {
        should_reconnect.store(true);
        std::thread([this]() {
            while (should_reconnect.load()) {
                if (!connected.load()) {
                    std::cout << "Attempting to reconnect to dataserver..." << std::endl;
                    if (connect()) {
                        std::cout << "Reconnected successfully!" << std::endl;
                    } else {
                        std::cout << "Reconnection failed, retrying in " << RECONNECT_DELAY_MS/1000 << " seconds..." << std::endl;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
            }
        }).detach();
    }
    
    void stopReconnectLoop() {
        should_reconnect.store(false);
    }
};

// Global variables
std::atomic<bool> running(true);
MPR121 cap0(0x5A);
MPR121 cap1(0x5B);

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -h, --host <address>    Dataserver host address (default: " << DEFAULT_DATASERVER_ADDRESS << ")\n"
              << "  -p, --port <port>       Dataserver port (default: " << DSERV_PORT << ")\n"
              << "  -t, --timer <ms>        Timer interval in milliseconds (default: " << DEFAULT_TIMER_INTERVAL_MS << ")\n"
              << "  --help                  Show this help message\n"
              << "\nExample:\n"
              << "  " << program_name << " -h 192.168.1.100 -p 4620 -t 50\n"
              << "  " << program_name << " --host server.local --timer 10  # 100Hz sampling\n"
              << std::endl;
}

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    running.store(false);
}

int main(int argc, char* argv[]) {
    std::string server_address = DEFAULT_DATASERVER_ADDRESS;
    int server_port = DSERV_PORT;
    int timer_interval_ms = DEFAULT_TIMER_INTERVAL_MS;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-h" || arg == "--host") {
            if (i + 1 < argc) {
                server_address = argv[++i];
            } else {
                std::cerr << "Error: " << arg << " requires an address argument" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        }
        else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                server_port = std::atoi(argv[++i]);
                if (server_port <= 0 || server_port > 65535) {
                    std::cerr << "Error: Invalid port number" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a port argument" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        }
        else if (arg == "-t" || arg == "--timer") {
            if (i + 1 < argc) {
                timer_interval_ms = std::atoi(argv[++i]);
                if (timer_interval_ms <= 0 || timer_interval_ms > 10000) {
                    std::cerr << "Error: Timer interval must be between 1-10000 ms" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a timer interval in milliseconds" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        }
        else {
            std::cerr << "Error: Unknown argument " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Create client with parsed arguments
    DataserverClient client(server_address, server_port);
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "Starting MPR121 Data Forwarder for Raspberry Pi" << std::endl;
    std::cout << "Target server: " << server_address << ":" << server_port << std::endl;
    std::cout << "Sample rate: " << (1000.0 / timer_interval_ms) << " Hz (" << timer_interval_ms << "ms interval)" << std::endl;
    
    // Initialize MPR121 sensors
    if (!cap0.begin()) {
        std::cerr << "MPR121 sensor 0 (0x5A) not found!" << std::endl;
        return 1;
    }
    std::cout << "MPR121[0] found!" << std::endl;
    
    if (!cap1.begin()) {
        std::cerr << "MPR121 sensor 1 (0x5B) not found!" << std::endl;
        return 1;
    }
    std::cout << "MPR121[1] found!" << std::endl;
    
    // Start reconnection loop
    client.startReconnectLoop();
    
    // Create timer for periodic readings
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd < 0) {
        std::cerr << "Failed to create timer: " << strerror(errno) << std::endl;
        return 1;
    }
    
    struct itimerspec timer_spec;
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_nsec = timer_interval_ms * 1000000; // Convert ms to ns
    timer_spec.it_interval.tv_sec = 0;
    timer_spec.it_interval.tv_nsec = timer_interval_ms * 1000000;
    
    if (timerfd_settime(timer_fd, 0, &timer_spec, NULL) < 0) {
        std::cerr << "Failed to set timer: " << strerror(errno) << std::endl;
        close(timer_fd);
        return 1;
    }
    
    // Tracking variables
    uint16_t last_touched0 = 0;
    uint16_t last_touched1 = 0;
    
    const char* sensor0_touched_point = "grasp/sensor0/touched";
    const char* sensor0_vals_point = "grasp/sensor0/vals";
    const char* sensor1_touched_point = "grasp/sensor1/touched";
    const char* sensor1_vals_point = "grasp/sensor1/vals";
    
    std::cout << "Starting data collection loop..." << std::endl;
    
    while (running.load()) {
        // Wait for timer to expire
        uint64_t timer_expirations;
        ssize_t bytes_read = read(timer_fd, &timer_expirations, sizeof(timer_expirations));
        
        if (bytes_read < 0) {
            if (errno == EINTR) continue; // Interrupted by signal
            std::cerr << "Timer read error: " << strerror(errno) << std::endl;
            break;
        }
        
        // Check touch status changes
        uint16_t curr_touched0 = cap0.touched();
        if (curr_touched0 != last_touched0) {
            if (client.isConnected() && client.testConnection()) {
                client.writeToDataserver(sensor0_touched_point, DSERV_SHORT, 
                                       sizeof(uint16_t), &curr_touched0);
            }
            last_touched0 = curr_touched0;
        }
        
        uint16_t curr_touched1 = cap1.touched();
        if (curr_touched1 != last_touched1) {
            if (client.isConnected() && client.testConnection()) {
                client.writeToDataserver(sensor1_touched_point, DSERV_SHORT,
                                       sizeof(uint16_t), &curr_touched1);
            }
            last_touched1 = curr_touched1;
        }
        
        // Send filtered data periodically (and test connection)
        if (client.testConnection()) {
            uint16_t filtered_data[NSENSORS];
            
            // Get sensor 0 data
            for (int i = 0; i < NSENSORS; i++) {
                filtered_data[i] = cap0.filteredData(i);
            }
            client.writeToDataserver(sensor0_vals_point, DSERV_SHORT,
                                   NSENSORS * sizeof(uint16_t), filtered_data);
            
            // Get sensor 1 data
            for (int i = 0; i < NSENSORS; i++) {
                filtered_data[i] = cap1.filteredData(i);
            }
            client.writeToDataserver(sensor1_vals_point, DSERV_SHORT,
                                   NSENSORS * sizeof(uint16_t), filtered_data);
        }
    }
    
    std::cout << "Cleaning up..." << std::endl;
    
    // Cleanup
    close(timer_fd);
    client.stopReconnectLoop();
    client.disconnect();
    
    std::cout << "Shutdown complete." << std::endl;
    return 0;
}