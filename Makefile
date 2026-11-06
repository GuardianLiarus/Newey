CXX := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter $(shell pkg-config --cflags gtk+-3.0 libusb-1.0)
LDFLAGS := $(shell pkg-config --libs gtk+-3.0 libusb-1.0) -static-libstdc++ -static-libgcc

SRCS := src/main.cpp src/wey_config.cpp src/wey_usb.cpp
TARGET := build/newey

all: $(TARGET)

$(TARGET): $(SRCS) src/wey_config.h src/wey_usb.h src/wey_resources.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
	strip $(TARGET)

clean:
	rm -rf build
