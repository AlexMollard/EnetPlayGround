#include "StackTrace.h"
#include <sstream>
#include <iostream>
#include <iomanip>

// Windows implementation
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <processthreadsapi.h>
#include <Dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#include <string>
#include <vector>

std::string StackTrace::capture(int skipFrames, int maxFrames) {
    const auto frames = captureFrames(skipFrames + 1, maxFrames); // Skip this function
    
    std::ostringstream ss;
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        ss << "\n  " << std::setw(2) << i << ": ";
        
        if (!frame.function.empty()) {
            ss << frame.function;
        } else {
            ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(frame.address);
        }
        
        if (!frame.filename.empty()) {
            ss << " in " << frame.filename;
            if (frame.line > 0) {
                ss << ":" << std::dec << frame.line;
            }
        }
    }
    
    return ss.str();
}

std::vector<StackTrace::StackFrame> StackTrace::captureFrames(int skipFrames, int maxFrames) {
    std::vector<StackFrame> frames;
    
    // Initialize symbols
    static bool symbolsInitialized = false;
    if (!symbolsInitialized) {
        DWORD options = SymGetOptions();
        options |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
        SymSetOptions(options);
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        symbolsInitialized = true;
    }
    
    // Capture stack trace
    void* stack[64]; // Max frames we can capture
    WORD numFrames = CaptureStackBackTrace(
        skipFrames + 1,  // Skip this function and CaptureStackBackTrace
        std::min(maxFrames, 64),
        stack,
        nullptr
    );
    
    HANDLE process = GetCurrentProcess();
    
    // Symbol buffer with extra space for name
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256, 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    
    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    
    // Process each frame
    for (int i = 0; i < numFrames; i++) {
        StackFrame frame;
        frame.address = stack[i];
        
        // Get function name
        if (SymFromAddr(process, (DWORD64)stack[i], 0, symbol)) {
            frame.function = std::string(symbol->Name);
        }
        
        // Get file and line
        DWORD displacement;
        if (SymGetLineFromAddr64(process, (DWORD64)stack[i], &displacement, &line)) {
            frame.filename = std::string(line.FileName);
            frame.line = line.LineNumber;
        }
        
        frames.push_back(frame);
    }
    
    free(symbol);
    return frames;
}

// Linux/Unix implementation
#else
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>

std::string StackTrace::capture(int skipFrames, int maxFrames) {
    const auto frames = captureFrames(skipFrames + 1, maxFrames); // Skip this function
    
    std::ostringstream ss;
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        ss << "\n  " << std::setw(2) << i << ": ";
        
        if (!frame.function.empty()) {
            ss << frame.function;
        } else {
            ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(frame.address);
        }
        
        if (!frame.filename.empty()) {
            ss << " in " << frame.filename;
            if (frame.line > 0) {
                ss << ":" << std::dec << frame.line;
            }
        }
    }
    
    return ss.str();
}

std::string StackTrace::demangle(const char* symbol) {
    int status;
    std::unique_ptr<char, decltype(&std::free)> demangled(
        abi::__cxa_demangle(symbol, nullptr, nullptr, &status),
        std::free
    );
    
    return (status == 0 && demangled) ? std::string(demangled.get()) : std::string(symbol);
}

std::vector<StackTrace::StackFrame> StackTrace::captureFrames(int skipFrames, int maxFrames) {
    std::vector<StackFrame> frames;
    
    // Capture the stack trace
    void* stack[128];
    int frameCount = backtrace(stack, std::min(maxFrames + skipFrames, 128));
    char** symbols = backtrace_symbols(stack, frameCount);
    
    if (symbols == nullptr) {
        return frames;
    }
    
    // Start from skipFrames
    for (int i = skipFrames; i < frameCount; ++i) {
        StackFrame frame;
        frame.address = stack[i];
        
        // Parse the backtrace_symbols output
        // Format is typically: module(function+offset) [address]
        std::string symbolStr(symbols[i]);
        
        size_t funcStart = symbolStr.find('(');
        size_t funcEnd = symbolStr.find('+', funcStart);
        
        if (funcStart != std::string::npos && funcEnd != std::string::npos) {
            // Extract the module name
            frame.filename = symbolStr.substr(0, funcStart);
            
            // Extract and demangle the function name
            std::string mangled = symbolStr.substr(funcStart + 1, funcEnd - funcStart - 1);
            if (!mangled.empty()) {
                frame.function = demangle(mangled.c_str());
            }
        } else {
            // Couldn't parse, use the whole string
            frame.function = symbolStr;
        }
        
        frames.push_back(frame);
    }
    
    free(symbols);
    return frames;
}
#endif