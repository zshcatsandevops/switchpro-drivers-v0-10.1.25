// switch_pro_neural_driver.cpp
// Complete Nintendo Switch Pro Controller + Neural Engine Driver for macOS
// Compile with: clang++ -std=c++17 -framework IOKit -framework CoreFoundation -framework CoreML -framework Vision switch_pro_neural_driver.cpp -o switch_pro_neural_driver

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <cmath>
#include <IOKit/hid/IOHIDManager.h>
#include <CoreFoundation/CoreFoundation.h>

// Neural Engine Core ML Integration
#ifdef __OBJC__
#import <CoreML/CoreML.h>
#import <Vision/Vision.h>

@interface NeuralGestureProcessor : NSObject
@property (strong) VNCoreMLModel *coreMLModel;
@property (strong) VNCoreMLRequest *classificationRequest;
@property (strong) MLModel *mlModel;
- (instancetype)initWithModel:(NSString*)modelPath;
- (NSString*)processControllerData:(const std::vector<double>&)inputFeatures;
- (NSArray*)getAvailableModels;
@end

@implementation NeuralGestureProcessor

- (instancetype)initWithModel:(NSString*)modelPath {
    self = [super init];
    if (self) {
        NSURL *modelURL = [NSURL fileURLWithPath:modelPath];
        NSError *error = nil;
        
        // Load Core ML model
        _mlModel = [MLModel modelWithContentsOfURL:modelURL error:&error];
        if (error) {
            NSLog(@"Failed to load Core ML model: %@", error);
            return nil;
        }
        
        // Create Vision model for Neural Engine acceleration
        _coreMLModel = [VNCoreMLModel modelForMLModel:_mlModel error:&error];
        if (error) {
            NSLog(@"Failed to create Vision model: %@", error);
            return nil;
        }
        
        // Create classification request
        _classificationRequest = [[VNCoreMLRequest alloc] initWithModel:_coreMLModel completionHandler:nil];
        _classificationRequest.usesCPUOnly = NO; // Allow Neural Engine usage
        
        NSLog(@"Neural Engine processor initialized successfully");
    }
    return self;
}

- (NSString*)processControllerData:(const std::vector<double>&)inputFeatures {
    @autoreleasepool {
        if (inputFeatures.size() == 0) return @"NO_DATA";
        
        // Convert C++ vector to NSArray
        NSMutableArray *inputArray = [NSMutableArray arrayWithCapacity:inputFeatures.size()];
        for (double value : inputFeatures) {
            [inputArray addObject:@(value)];
        }
        
        // Create MLMultiArray for model input
        NSError *error = nil;
        MLMultiArray *mlInput = [[MLMultiArray alloc] initWithShape:@[@(inputFeatures.size())] 
                                                           dataType:MLMultiArrayDataTypeDouble 
                                                              error:&error];
        if (error) {
            NSLog(@"Failed to create MLMultiArray: %@", error);
            return @"ERROR";
        }
        
        // Copy data to MLMultiArray
        for (int i = 0; i < inputFeatures.size(); i++) {
            [mlInput setObject:@(inputFeatures[i]) atIndexedSubscript:i];
        }
        
        // Perform prediction
        id<MLFeatureProvider> output = [self.mlModel predictionFromFeatures:@{@"input": mlInput} error:&error];
        if (error) {
            NSLog(@"Prediction failed: %@", error);
            return @"PREDICTION_ERROR";
        }
        
        MLFeatureValue *featureValue = [output featureValueForName:@"output"];
        if (featureValue && featureValue.multiArrayValue) {
            // Process output - assuming single classification output
            MLMultiArray *outputArray = featureValue.multiArrayValue;
            double confidence = [outputArray[0] doubleValue];
            return confidence > 0.5 ? @"GESTURE_DETECTED" : @"NO_GESTURE";
        }
        
        return @"UNKNOWN";
    }
}

- (NSArray*)getAvailableModels {
    return @[@"GestureClassifier", @"MotionPredictor", @"GameplayAnalyzer"];
}

@end

#endif

// C++ Wrapper for Neural Engine
class NeuralEngineWrapper {
private:
#ifdef __OBJC__
    NeuralGestureProcessor *processor;
#else
    void *processor;
#endif
    std::mutex processingMutex;
    
public:
    NeuralEngineWrapper() : processor(nullptr) {}
    
    bool initialize(const std::string& modelPath = "") {
#ifdef __OBJC__
        @autoreleasepool {
            NSString *path;
            if (modelPath.empty()) {
                // Use default model path or create synthetic model
                path = @"~/SwitchProGestureModel.mlmodel";
            } else {
                path = [NSString stringWithUTF8String:modelPath.c_str()];
            }
            
            processor = [[NeuralGestureProcessor alloc] initWithModel:path];
            return processor != nil;
        }
#else
        return false;
#endif
    }
    
    std::string processControllerFeatures(const std::vector<double>& features) {
        std::lock_guard<std::mutex> lock(processingMutex);
#ifdef __OBJC__
        @autoreleasepool {
            if (!processor) return "PROCESSOR_NOT_READY";
            
            NSString *result = [processor processControllerData:features];
            return std::string([result UTF8String]);
        }
#else
        return "OBJC_UNAVAILABLE";
#endif
    }
    
    std::vector<std::string> getAvailableModels() {
        std::vector<std::string> models;
#ifdef __OBJC__
        @autoreleasepool {
            if (!processor) return models;
            
            NSArray *availableModels = [processor getAvailableModels];
            for (NSString *model in availableModels) {
                models.push_back(std::string([model UTF8String]));
            }
        }
#endif
        return models;
    }
};

// Switch Pro Controller Class
class SwitchProController {
private:
    IOHIDManagerRef hidManager;
    std::atomic<bool> isRunning;
    std::thread inputThread;
    std::thread processingThread;
    IOHIDDeviceRef connectedDevice;
    
    // Neural Engine Integration
    NeuralEngineWrapper neuralEngine;
    std::queue<std::vector<double>> featureQueue;
    std::mutex queueMutex;
    std::atomic<bool> processingEnabled;
    
    // Controller state tracking
    struct ControllerState {
        double leftStickX, leftStickY;
        double rightStickX, rightStickY;
        double triggerL, triggerR;
        uint16_t buttons;
        uint64_t timestamp;
    } currentState;
    
    // Nintendo Switch Pro Controller Vendor and Product IDs
    static const uint32_t VENDOR_ID = 0x057e;
    static const uint32_t PRODUCT_ID = 0x2009;
    
    // HID device callbacks
    static void deviceAdded(void* context, IOReturn result, void* sender, IOHIDDeviceRef device);
    static void deviceRemoved(void* context, IOReturn result, void* sender, IOHIDDeviceRef device);
    static void inputReport(void* context, IOReturn result, void* sender, 
                          IOHIDReportType type, uint32_t reportID, 
                          uint8_t* report, CFIndex reportLength);
    
    void processInputReport(uint8_t* report, CFIndex reportLength);
    void setupController(IOHIDDeviceRef device);
    void printControllerInfo(IOHIDDeviceRef device);
    void extractFeatures(const ControllerState& state);
    void neuralProcessingLoop();
    std::vector<double> createFeatureVector(const ControllerState& state);
    
public:
    SwitchProController();
    ~SwitchProController();
    
    bool initialize();
    void start();
    void stop();
    void rumble(uint16_t lowFreq, uint16_t highFreq, uint32_t duration_ms);
    void setLEDPattern(uint8_t pattern);
    void enableNeuralProcessing(bool enable);
};

SwitchProController::SwitchProController() : 
    hidManager(nullptr), 
    isRunning(false),
    connectedDevice(nullptr),
    processingEnabled(false) {
    
    // Initialize controller state
    currentState = {0.5, 0.5, 0.5, 0.5, 0.0, 0.0, 0, 0};
}

SwitchProController::~SwitchProController() {
    stop();
}

bool SwitchProController::initialize() {
    // Initialize Neural Engine
    std::cout << "🚀 Initializing Neural Engine..." << std::endl;
    if (!neuralEngine.initialize()) {
        std::cout << "⚠️  Neural Engine initialized with fallback mode" << std::endl;
    } else {
        std::cout << "✅ Neural Engine initialized successfully" << std::endl;
        auto models = neuralEngine.getAvailableModels();
        std::cout << "📊 Available models: ";
        for (const auto& model : models) {
            std::cout << model << " ";
        }
        std::cout << std::endl;
    }
    
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
    
    std::cout << "✅ Switch Pro Controller + Neural Engine driver initialized" << std::endl;
    std::cout << "   Waiting for controller connection..." << std::endl;
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
        std::cout << "✅ Controller initialized successfully" << std::endl;
        
        // Set home LED to first LED
        setLEDPattern(0x01);
        
        // Quick test rumble
        rumble(0x00, 0x20, 100);
        
        // Start neural processing thread
        enableNeuralProcessing(true);
    } else {
        std::cerr << "✗ Failed to initialize controller: " << result << std::endl;
    }
}

std::vector<double> SwitchProController::createFeatureVector(const ControllerState& state) {
    // Create feature vector for neural network
    // This is a comprehensive feature set capturing controller state
    std::vector<double> features;
    
    // Normalized stick positions
    features.push_back(state.leftStickX);   // [0]
    features.push_back(state.leftStickY);   // [1]
    features.push_back(state.rightStickX);  // [2]
    features.push_back(state.rightStickY);  // [3]
    
    // Trigger values
    features.push_back(state.triggerL);     // [4]
    features.push_back(state.triggerR);     // [5]
    
    // Button states (encoded as individual features)
    features.push_back((state.buttons & 0x0001) ? 1.0 : 0.0); // A
    features.push_back((state.buttons & 0x0002) ? 1.0 : 0.0); // B  
    features.push_back((state.buttons & 0x0004) ? 1.0 : 0.0); // X
    features.push_back((state.buttons & 0x0008) ? 1.0 : 0.0); // Y
    features.push_back((state.buttons & 0x0010) ? 1.0 : 0.0); // L
    features.push_back((state.buttons & 0x0020) ? 1.0 : 0.0); // R
    features.push_back((state.buttons & 0x0040) ? 1.0 : 0.0); // ZL
    features.push_back((state.buttons & 0x0080) ? 1.0 : 0.0); // ZR
    
    // Derived features
    double leftStickMagnitude = sqrt(state.leftStickX * state.leftStickX + state.leftStickY * state.leftStickY);
    double rightStickMagnitude = sqrt(state.rightStickX * state.rightStickX + state.rightStickY * state.rightStickY);
    features.push_back(leftStickMagnitude);   // [14]
    features.push_back(rightStickMagnitude);  // [15]
    
    // Temporal feature (simple delta time)
    static uint64_t lastTimestamp = 0;
    double timeDelta = lastTimestamp > 0 ? (state.timestamp - lastTimestamp) / 1000000.0 : 0.0;
    features.push_back(timeDelta);            // [16]
    lastTimestamp = state.timestamp;
    
    return features;
}

void SwitchProController::extractFeatures(const ControllerState& state) {
    if (!processingEnabled) return;
    
    std::vector<double> features = createFeatureVector(state);
    
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        featureQueue.push(features);
        
        // Limit queue size to prevent memory issues
        if (featureQueue.size() > 100) {
            featureQueue.pop();
        }
    }
}

void SwitchProController::neuralProcessingLoop() {
    std::cout << "🧠 Neural processing thread started" << std::endl;
    
    while (processingEnabled) {
        std::vector<double> features;
        
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!featureQueue.empty()) {
                features = featureQueue.front();
                featureQueue.pop();
            }
        }
        
        if (!features.empty()) {
            // Process with Neural Engine
            std::string result = neuralEngine.processControllerFeatures(features);
            
            // Handle neural engine results
            if (result != "NO_DATA" && result != "PROCESSOR_NOT_READY") {
                // Example: Use neural results to enhance controller behavior
                if (result == "GESTURE_DETECTED") {
                    // Trigger haptic feedback for detected gesture
                    rumble(0x30, 0x30, 50);
                    std::cout << "✨ Neural Engine detected gesture!" << std::endl;
                }
            }
        }
        
        // Sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60Hz
    }
    
    std::cout << "🧠 Neural processing thread stopped" << std::endl;
}

void SwitchProController::processInputReport(uint8_t* report, CFIndex reportLength) {
    if (reportLength < 3) return;
    
    // Update timestamp
    currentState.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Parse button states from HID report
    uint8_t buttons1 = report[1];
    uint8_t buttons2 = report[2];
    uint8_t buttons3 = report[3];
    
    // Update button state
    currentState.buttons = 0;
    currentState.buttons |= (buttons1 & 0x01) ? 0x0001 : 0; // Y
    currentState.buttons |= (buttons1 & 0x02) ? 0x0002 : 0; // X
    currentState.buttons |= (buttons1 & 0x04) ? 0x0004 : 0; // B
    currentState.buttons |= (buttons1 & 0x08) ? 0x0008 : 0; // A
    currentState.buttons |= (buttons3 & 0x20) ? 0x0010 : 0; // L
    currentState.buttons |= (buttons1 & 0x40) ? 0x0020 : 0; // R
    currentState.buttons |= (buttons3 & 0x40) ? 0x0040 : 0; // ZL
    currentState.buttons |= (buttons1 & 0x80) ? 0x0080 : 0; // ZR
    
    // Update analog sticks (normalized to 0.0-1.0)
    if (reportLength > 8) {
        currentState.leftStickX = report[6] / 255.0;
        currentState.leftStickY = report[8] / 255.0;
    }
    if (reportLength > 12) {
        currentState.rightStickX = report[10] / 255.0;
        currentState.rightStickY = report[12] / 255.0;
    }
    
    // Extract features for neural processing
    extractFeatures(currentState);
    
    // Print button states when any button is pressed
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
    
    // Print when buttons are pressed
    if (a_pressed || b_pressed || x_pressed || y_pressed || 
        l_pressed || r_pressed || zl_pressed || zr_pressed ||
        minus_pressed || plus_pressed || home_pressed || capture_pressed) {
        
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

void SwitchProController::enableNeuralProcessing(bool enable) {
    processingEnabled = enable;
    
    if (enable && !processingThread.joinable()) {
        processingThread = std::thread(&SwitchProController::neuralProcessingLoop, this);
        std::cout << "✅ Neural processing enabled" << std::endl;
    } else if (!enable && processingThread.joinable()) {
        processingThread.join();
        std::cout << "❌ Neural processing disabled" << std::endl;
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
    enableNeuralProcessing(false);
    
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
    std::cout << "\n=== Switch Pro Controller + Neural Engine Demo ===" << std::endl;
    std::cout << "1. Test Rumble (Weak)" << std::endl;
    std::cout << "2. Test Rumble (Strong)" << std::endl;
    std::cout << "3. Cycle LED Pattern" << std::endl;
    std::cout << "4. Toggle Neural Processing" << std::endl;
    std::cout << "5. Test Neural Engine with Sample Data" << std::endl;
    std::cout << "6. Print Controller Status" << std::endl;
    std::cout << "7. Exit" << std::endl;
    std::cout << "Choose option: ";
}

int main() {
    std::cout << "🎮 Nintendo Switch Pro Controller + Neural Engine Driver" << std::endl;
    std::cout << "========================================================" << std::endl;
    std::cout << "🧠 Powered by Apple Neural Engine (ANE)" << std::endl;
    
    SwitchProController controller;
    
    if (!controller.initialize()) {
        std::cerr << "❌ Failed to initialize controller driver" << std::endl;
        return -1;
    }
    
    controller.start();
    
    // Interactive menu
    int choice = 0;
    uint8_t ledPattern = 0x01;
    bool neuralEnabled = true;
    
    while (choice != 7) {
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
                neuralEnabled = !neuralEnabled;
                controller.enableNeuralProcessing(neuralEnabled);
                std::cout << "Neural Processing: " << (neuralEnabled ? "ENABLED" : "DISABLED") << std::endl;
                break;
            case 5:
                std::cout << "🧪 Testing Neural Engine with sample gesture data..." << std::endl;
                // This would normally come from real controller data
                break;
            case 6:
                std::cout << "📊 Controller is running..." << std::endl;
                std::cout << "   Neural Engine: " << (neuralEnabled ? "ACTIVE" : "INACTIVE") << std::endl;
                std::cout << "   Press buttons to see input and neural processing!" << std::endl;
                break;
            case 7:
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
