# Makefile for Switch Pro Controller Driver
CXX = clang++
CXXFLAGS = -std=c++17 -Wall -framework IOKit -framework CoreFoundation -framework Carbon
TARGET = switch_pro_driver
SOURCES = switch_pro_driver.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	# Note: You might need to run this with sudo
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install
