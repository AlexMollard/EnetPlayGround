#pragma once

#include <string>
#include <vector>
#include <memory>
#include <source_location>

class StackTrace {
public:
    // Capture the current stack trace
    static std::string capture(int skipFrames = 0, int maxFrames = 32);
    
    // Internal frame structure
    struct StackFrame {
        void* address;
        std::string function;
        std::string filename;
        int line;
        
        StackFrame() : address(nullptr), line(0) {}
    };
    
    // Get detailed frames (platform specific implementations)
    static std::vector<StackFrame> captureFrames(int skipFrames = 0, int maxFrames = 32);
    
private:
    // Platform-specific implementations
    static std::string demangle(const char* symbol);
};