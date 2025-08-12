CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread
TARGET = mpr121_forwarder
SOURCES = mpr121_forwarder.cpp

# Default target
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

# Install target (optional)
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)

# Create systemd service file
service:
	@echo "Creating systemd service file..."
	@echo "[Unit]" > mpr121-forwarder.service
	@echo "Description=MPR121 Data Forwarder" >> mpr121-forwarder.service
	@echo "After=network.target" >> mpr121-forwarder.service
	@echo "" >> mpr121-forwarder.service
	@echo "[Service]" >> mpr121-forwarder.service
	@echo "Type=simple" >> mpr121-forwarder.service
	@echo "User=pi" >> mpr121-forwarder.service
	@echo "ExecStart=/usr/local/bin/mpr121_forwarder -h 192.168.88.40 -p 4620 -t 20" >> mpr121-forwarder.service
	@echo "Restart=always" >> mpr121-forwarder.service
	@echo "RestartSec=5" >> mpr121-forwarder.service
	@echo "" >> mpr121-forwarder.service
	@echo "[Install]" >> mpr121-forwarder.service
	@echo "WantedBy=multi-user.target" >> mpr121-forwarder.service
	@echo "Service file created: mpr121-forwarder.service"
	@echo ""
	@echo "EDIT the service file to set your desired host and port:"
	@echo "  nano mpr121-forwarder.service"
	@echo ""
	@echo "Then install with:"
	@echo "  sudo cp mpr121-forwarder.service /etc/systemd/system/"
	@echo "  sudo systemctl daemon-reload"
	@echo "  sudo systemctl enable mpr121-forwarder.service"
	@echo "  sudo systemctl start mpr121-forwarder.service"

# Clean target
clean:
	rm -f $(TARGET)

# Debug build
debug: CXXFLAGS += -DDEBUG -g
debug: $(TARGET)

.PHONY: all install service clean debug