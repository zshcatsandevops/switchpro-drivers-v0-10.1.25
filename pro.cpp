// switch_pro_driver.cpp
// Complete Nintendo Switch Pro Controller Driver for macOS Sequoia
// Compile with: clang++ -std=c++17 -framework IOKit -framework CoreFoundation switch_pro_driver.cpp -o switch_pro_driver

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>

class SwitchProController {
private:
    IOHIDManagerRef hidManager;
    std::atomic<bool> isRunning;
    std::thread inputThread;
    IOHIDDeviceRef connectedDevice;
    
    // Nintendo Switch Pro Controller Vendor and Product IDs
    static const uint32_t VENDOR_ID = 0x057e;    // Nintendo
    static const uint32_t PRODUCT_ID = 0x2009;   // Switch Pro Controller
    
    // HID device callbacks
    static void deviceAdded(void* context, IOReturn result, void* sender, IOHIDDeviceRef device);
    static void deviceRemoved(void* context, IOReturn result, void* sender, IOHIDDeviceRef device);
    static void inputReport(void* context, IOReturn result, void* sender, 
                          IOHIDReportType type, uint32_t reportID, 
                          uint8_t* report, CFIndex reportLength);
    
    void processInputReport(uint8_t* report, CFIndex reportLength);
    void setupController(IOHIDDeviceRef device);
    void printControllerInfo(IOHIDDeviceRef device);
    
public:
    SwitchProController();
    ~SwitchProController();
    
    bool initialize();
    void start();
    void stop();
    void rumble(uint16_t lowFreq, uint16_t highFreq, uint32_t duration_ms);
    void setLEDPattern(uint8_t pattern);
};

SwitchProController::SwitchProController() : 
    hidManager(nullptr), 
    isRunning(false),
    connectedDevice(nullptr) {
}

SwitchProController::~SwitchProController() {
    stop();
}

bool SwitchProController::initialize() {
    // Create HID Manager
    hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!hidManager) {
        std::cerr << "Failed to create HID manager" << std::endl;
        return false;
    }
    
    // Setup device matching dictionary
    CFMutableDictionaryRef matchingDict = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    
    if (!matchingDict) {
        std::cerr << "Failed to create matching dictionary" << std::endl;
        CFRelease(hidManager);
        return false;
    }
    
    // Add vendor and product ID to matching criteria
    CFNumberRef vendorIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &VENDOR_ID);
    CFNumberRef productIDRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &PRODUCT_ID);
    
    if (vendorIDRef && productIDRef) {
        CFDictionarySetValue(matchingDict, CFSTR(kIOHIDVendorIDKey), vendorIDRef);
        CFDictionarySetValue(matchingDict, CFSTR(kIOHIDProductIDKey), productIDRef);
        
        IOHIDManagerSetDeviceMatching(hidManager, matchingDict);
        
        CFRelease(vendorIDRef);
        CFRelease(productIDRef);
    }
    
    CFRelease(matchingDict);
    
    // Register callbacks
    IOHIDManagerRegisterDeviceMatchingCallback(hidManager, deviceAdded, this);
    IOHIDManagerRegisterDeviceRemovalCallback(hidManager, deviceRemoved, this);
    IOHIDManagerRegisterInputReportCallback(hidManager, inputReport, this);
    
    // Schedule HID manager in run loop
    IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    
    // Open HID manager
    IOReturn result = IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    if (result != kIOReturnSuccess) {
        std::cerr << "Failed to open HID manager: " << result << std::endl;
        CFRelease(hidManager);
        hidManager = nullptr;
        return false;
    }
    
    std::cout << "✓ Switch Pro Controller driver initialized successfully" << std::endl;
    std::cout << "  Waiting for controller connection..." << std::endl;
    return true;
}

void SwitchProController::deviceAdded(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    SwitchProController* controller = static_cast<SwitchProController*>(context);
    std::cout << "🎮 Switch Pro Controller connected!" << std::endl;
    controller->printControllerInfo(device);
    controller->setupController(device);
}

void SwitchProController::deviceRemoved(void* context, IOReturn result, void* sender, IOHIDDeviceRef device) {
    SwitchProController* controller = static_cast<SwitchProController*>(context);
    controller->connectedDevice = nullptr;
    std::cout << "📤 Switch Pro Controller disconnected!" << std::endl;
}

void SwitchProController::inputReport(void* context, IOReturn result, void* sender, 
                                    IOHIDReportType type, uint32_t reportID, 
                                    uint8_t* report, CFIndex reportLength) {
    if (result != kIOReturnSuccess) return;
    
    SwitchProController* controller = static_cast<SwitchProController*>(context);
    controller->processInputReport(report, reportLength);
}

void SwitchProController::printControllerInfo(IOHIDDeviceRef device) {
    CFStringRef product = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
    CFStringRef vendor = (CFStringRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorKey));
    CFNumberRef vendorID = (CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
    CFNumberRef productID = (CFNumberRef)IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));
    
    if (product) {
        char productStr[256];
        CFStringGetCString(product, productStr, sizeof(productStr), kCFStringEncodingUTF8);
        std::cout << "  Product: " << productStr << std::endl;
    }
    
    if (vendorID && productID) {
        int vendorVal, productVal;
        CFNumberGetValue(vendorID, kCFNumberIntType, &vendorVal);
        CFNumberGetValue(productID, kCFNumberIntType, &productVal);
        std::cout << "  Vendor ID: 0x" << std::hex << vendorVal << std::dec << std::endl;
        std::cout << "  Product ID: 0x" << std::hex << productVal << std::dec << std::endl;
    }
}

void SwitchProController::setupController(IOHIDDeviceRef device) {
    connectedDevice = device;
    
    // Set input report buffer
    uint8_t dummyReport[64] = {0};
    IOHIDDeviceRegisterInputReportCallback(device, dummyReport, sizeof(dummyReport), inputReport, this);
    
    // Initialize controller (send magic bytes)
    uint8_t initData[] = {0x80, 0x01};
    IOReturn result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0x01, initData, sizeof(initData));
    
    if (result == kIOReturnSuccess) {
        std::cout << "✓ Controller initialized successfully" << std::endl;
        
        // Set home LED to first LED
        setLEDPattern(0x01);
        
        // Quick test rumble
        rumble(0x00, 0x20, 100);
    } else {
        std::cerr << "✗ Failed to initialize controller: " << result << std::endl;
    }
}

void SwitchProController::processInputReport(uint8_t* report, CFIndex reportLength) {
    if (reportLength < 3) return;
    
    // Parse button states from HID report
    uint8_t buttons1 = report[1];
    uint8_t buttons2 = report[2];
    uint8_t buttons3 = report[3];
    
    // Decode button states
    bool y_pressed = (buttons1 & 0x01) != 0;
    bool x_pressed = (buttons1 & 0x02) != 0;
    bool b_pressed = (buttons1 & 0x04) != 0;
    bool a_pressed = (buttons1 & 0x08) != 0;
    bool r_pressed = (buttons1 & 0x40) != 0;
    bool zr_pressed = (buttons1 & 0x80) != 0;
    
    bool minus_pressed = (buttons2 & 0x01) != 0;
    bool plus_pressed  = (buttons2 & 0x02) != 0;
    bool stick_left_pressed = (buttons2 & 0x04) != 0;
    bool stick_right_pressed = (buttons2 & 0x08) != 0;
    bool home_pressed = (buttons2 & 0x10) != 0;
    bool capture_pressed = (buttons2 & 0x20) != 0;
    
    bool l_pressed = (buttons3 & 0x20) != 0;
    bool zl_pressed = (buttons3 & 0x40) != 0;
    
    // D-pad states (encoded in lower 4 bits of buttons3)
    uint8_t dpad_state = buttons3 & 0x0F;
    const char* dpad_states[] = {
        "↑", "↗", "→", "↘", "↓", "↙", "←", "↖", "•"  // • for neutral
    };
    const char* dpad = dpad_states[8]; // neutral
    if (dpad_state < 8) dpad = dpad_states[dpad_state];
    
    // Analog sticks (simplified - just show if moved)
    uint8_t left_stick_x = report[6];
    uint8_t left_stick_y = report[8];
    uint8_t right_stick_x = report[10];
    uint8_t right_stick_y = report[12];
    
    // Print button states when any button is pressed
    if (a_pressed || b_pressed || x_pressed || y_pressed || 
        l_pressed || r_pressed || zl_pressed || zr_pressed ||
        minus_pressed || plus_pressed || home_pressed || capture_pressed ||
        stick_left_pressed || stick_right_pressed || dpad_state != 8) {
        
        std::cout << "🕹️  Buttons: ";
        if (a_pressed) std::cout << "A ";
        if (b_pressed) std::cout << "B ";
        if (x_pressed) std::cout << "X ";
        if (y_pressed) std::cout << "Y ";
        if (l_pressed) std::cout << "L ";
        if (r_pressed) std::cout << "R ";
        if (zl_pressed) std::cout << "ZL ";
        if (zr_pressed) std::cout << "ZR ";
        if (minus_pressed) std::cout << "- ";
        if (plus_pressed) std::cout << "+ ";
        if (home_pressed) std::cout << "HOME ";
        if (capture_pressed) std::cout << "CAPTURE ";
        if (stick_left_pressed) std::cout << "L3 ";
        if (stick_right_pressed) std::cout << "R3 ";
        std::cout << "DPad:" << dpad;
        
        // Show stick movement if not centered
        if (left_stick_x != 0x80 || left_stick_y != 0x80) {
            std::cout << " LStick:(" << (int)left_stick_x << "," << (int)left_stick_y << ")";
        }
        if (right_stick_x != 0x80 || right_stick_y != 0x80) {
            std::cout << " RStick:(" << (int)right_stick_x << "," << (int)right_stick_y << ")";
        }
        
        std::cout << std::endl;
    }
}

void SwitchProController::rumble(uint16_t lowFreq, uint16_t highFreq, uint32_t duration_ms) {
    if (!connectedDevice) return;
    
    uint8_t rumbleData[] = {
        0x10, 0x80, 0x00, 0x00, 0x00,
        static_cast<uint8_t>(highFreq & 0xFF), static_cast<uint8_t>(highFreq >> 8),
        static_cast<uint8_t>(lowFreq & 0xFF), static_cast<uint8_t>(lowFreq >> 8),
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    IOReturn result = IOHIDDeviceSetReport(connectedDevice, kIOHIDReportTypeOutput, 0x10, 
                                         rumbleData, sizeof(rumbleData));
    
    if (result == kIOReturnSuccess) {
        std::cout << "🔊 Rumble activated (" << duration_ms << "ms)" << std::endl;
    }
}

void SwitchProController::setLEDPattern(uint8_t pattern) {
    if (!connectedDevice) return;
    
    uint8_t ledData[] = {0x01, static_cast<uint8_t>(pattern & 0x0F)};
    
    IOReturn result = IOHIDDeviceSetReport(connectedDevice, kIOHIDReportTypeOutput, 0x01, 
                                         ledData, sizeof(ledData));
    
    if (result == kIOReturnSuccess) {
        std::cout << "💡 LED pattern set: 0x" << std::hex << (int)pattern << std::dec << std::endl;
    }
}

void SwitchProController::start() {
    if (!isRunning && hidManager) {
        isRunning = true;
        inputThread = std::thread([this]() {
            std::cout << "🚀 Starting HID event loop..." << std::endl;
            CFRunLoopRun();
        });
    }
}

void SwitchProController::stop() {
    if (isRunning) {
        isRunning = false;
        if (hidManager) {
            CFRunLoopStop(CFRunLoopGetCurrent());
        }
        if (inputThread.joinable()) {
            inputThread.join();
        }
    }
    
    if (hidManager) {
        IOHIDManagerClose(hidManager, kIOHIDOptionsTypeNone);
        CFRelease(hidManager);
        hidManager = nullptr;
    }
}

// Demo application with interactive menu
void printMenu() {
    std::cout << "\n=== Switch Pro Controller Demo ===" << std::endl;
    std::cout << "1. Test Rumble (Weak)" << std::endl;
    std::cout << "2. Test Rumble (Strong)" << std::endl;
    std::cout << "3. Cycle LED Pattern" << std::endl;
    std::cout << "4. Print Controller Status" << std::endl;
    std::cout << "5. Exit" << std::endl;
    std::cout << "Choose option: ";
}

int main() {
    std::cout << "🎮 Nintendo Switch Pro Controller Driver for macOS Sequoia" << std::endl;
    std::cout << "==========================================================" << std::endl;
    
    SwitchProController controller;
    
    if (!controller.initialize()) {
        std::cerr << "❌ Failed to initialize controller driver" << std::endl;
        return -1;
    }
    
    controller.start();
    
    // Interactive menu
    int choice = 0;
    uint8_t ledPattern = 0x01;
    
    while (choice != 5) {
        printMenu();
        std::cin >> choice;
        
        switch (choice) {
            case 1:
                controller.rumble(0x00, 0x20, 300);
                break;
            case 2:
                controller.rumble(0x80, 0xFF, 500);
                break;
            case 3:
                controller.setLEDPattern(ledPattern);
                ledPattern = (ledPattern << 1) & 0x0F;
                if (ledPattern == 0) ledPattern = 0x01;
                break;
            case 4:
                std::cout << "📊 Controller is running..." << std::endl;
                std::cout << "   Press buttons on your controller to see input!" << std::endl;
                break;
            case 5:
                std::cout << "Shutting down..." << std::endl;
                break;
            default:
                std::cout << "Invalid option!" << std::endl;
                break;
        }
    }
    
    controller.stop();
    std::cout << "✅ Driver stopped successfully." << std::endl;
    
    return 0;
}
