/***********************************************************************
** Copyright (C) 2018, Joey de Vries
** 
** ShaderAssist is free software: you can redistribute it and/or modify
** it under the terms of the CC BY 4.0 license as published by Creative 
** Commons, either version 4 of the License, or (at your option) any 
** later version. 
***********************************************************************/

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
#include <array>
#include <map>

#ifdef _MSC_VER
    namespace fs = std::experimental::filesystem;
#elif defined __GNUC__ || defined __MINGW32__ || defined __MINGW64__ // note: these compilers have not been tested for support non-experimental filesystem
    namespace fs = std::filesystem;
#else 
    namespace fs = std::filesystem;
#endif

// Configuration (values extracted from .ini file)
// ----------------------------------------------------------------------------------
struct Config {
    // compile all shaders on startup ShaderAssist
    bool CompileOnStartup;
    // use Google's SPIR-V compiler (more features including preprocess #include support)
    bool UseGoogleSPIRV;
    // generate SPIR-V metadata (useful for automatic pipeline/descriptor generation) using spirv-cross
    bool GenerateMetaData;
    // path to the Vulkan SPIR-V compiler
    std::string GLSLLangValidatorPath;
    // path to the Google SPIR-V compiler
    std::string GLSLCPath;
    // folder to read/check for modified shader source files (use / for absolute paths)
    std::string ShaderSourcePath;
    // output compiled SPIRV path (use / for absolute paths)
    std::string SPIRVOutputPath;
    // SPIRV output extension
    std::string SPIRVExt;
    // vertex shader extension
    std::string VSExt;
    // fragment shader extension
    std::string FSExt;
    // compute shader extension
    std::string CSExt;
    // geometry shader extension
    std::string GSExt;
} config;

// Global state
// ------------
static std::atomic<bool> sApplicationExit = false;
static std::atomic<bool> sRecompile       = false;
static bool              sFirstIteration  = true;

// Data structure for each shader file that's being watched
// --------------------------------------------------------
struct ShaderEntry {
    fs::file_time_type  LastWriteTime;
};
// Store file data for all shader that's being watched
static std::map<fs::path, ShaderEntry> sShaderEntries;

// Compile shader to SPIRV
// -----------------------
void compileShader(std::string filename, std::string ext) {
    std::string command = "";
    if(config.UseGoogleSPIRV) {
        command = config.GLSLCPath + " " + filename + ext + " -o " + config.SPIRVOutputPath + "/" + filename + ext + config.SPIRVExt;
    } else {
        command = config.GLSLLangValidatorPath + " - V " + filename + ext + " -o " + config.SPIRVOutputPath + "/" + filename + ext + config.SPIRVExt;
    }
#ifdef _WIN32
    system((command + " > nul").c_str());
#elif defined __linux__ || defined __unix__ 
    system((command + " > /dev/null").c_str());
#else 
    system(command.c_str());
#endif
}

// Continously checks all shader files in .ini-specified directory for modifications and automatically compile to SPIRV when modified
// ----------------------------------------------------------------------------------------------------------------------------------
void watchShaders(fs::path path) {
    // Get a reference to each shader file in this directory (repeat this every time in case new files get added)
    while(!sApplicationExit) {
        for(auto& p : fs::directory_iterator(path)) {
            if(fs::is_regular_file(p)) {
                std::string filename    = fs::path(p).stem().string();
                std::string extension   = fs::path(p).extension().string();

                std::array<std::string, 4> validFileExts = { config.VSExt, config.FSExt, config.GSExt, config.CSExt };
                if(std::find(validFileExts.begin(), validFileExts.end(), extension) != validFileExts.end()) {
                    if(sShaderEntries.find(p) != sShaderEntries.end()) {
                        // Compare timestamps, if it's different; re-compile
                        fs::file_time_type currFileTimeType = fs::last_write_time(p);
                        std::time_t lastWriteTime    = fs::file_time_type::clock::to_time_t(sShaderEntries[p].LastWriteTime);;
                        std::time_t currentWriteTime = fs::file_time_type::clock::to_time_t(currFileTimeType);
                        if(currentWriteTime - lastWriteTime > 1 || sRecompile) { // In seconds
                            // File has been adjusted, re-compile
                            std::cout << "- File " << filename + extension << " is modified, recompiling..." << std::endl;
                            compileShader(filename, extension);
                            // And update time stamp
                            sShaderEntries[p].LastWriteTime = currFileTimeType;
                        }
                    } else {
                        // Newly added shader; add to entry and compile
                        sShaderEntries[p] = { fs::last_write_time(p) };

                        // Don't compile the first iteration (unless specified in .ini) as every shader checked will satisfy time delta requirement first run
                        if(!sFirstIteration || config.CompileOnStartup) {
                            std::cout << "- Newly recognized file: " << filename + extension << ", compiling..." << std::endl;
                            compileShader(filename, extension);
                        }
                    }
                }
            }
        }
        sFirstIteration = false;
        sRecompile      = false;
        // Wait for 1 second and check again (don't stress the CPU)
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

// Parse config values from the .ini file
// --------------------------------------
void parseIniFile(std::ifstream& iniFile) {
    std::map<std::string, std::string> iniKeyValuePairs;

    std::string line;
    while(std::getline(iniFile, line)) {
        if(line[0] != '#') {
            iniKeyValuePairs[line.substr(0, line.find('='))] = line.substr(line.find('=') + 1);
        }
    }
    config.CompileOnStartup         = iniKeyValuePairs["compile_on_startup"] == "true" ? true : false;
    config.UseGoogleSPIRV           = iniKeyValuePairs["use_google_spirv"]   == "true" ? true : false;
    config.GLSLLangValidatorPath    = iniKeyValuePairs["glsl_lang_validator_path"];
    config.GLSLCPath                = iniKeyValuePairs["glsl_c_path"];
    config.ShaderSourcePath         = iniKeyValuePairs["shader_source_path"];
    config.SPIRVOutputPath          = iniKeyValuePairs["spirv_output_path"];
    config.SPIRVExt                 = iniKeyValuePairs["spirv_ext"];
    config.VSExt                    = iniKeyValuePairs["vs_ext"];
    config.FSExt                    = iniKeyValuePairs["fs_ext"];
    config.GSExt                    = iniKeyValuePairs["gs_ext"];
    config.CSExt                    = iniKeyValuePairs["cs_ext"];
}

// Program entry
// -------------
int main(int argc, char** argv) {
    fs::path currentPath = fs::current_path();

    // Extract configuration from .ini file 
    std::ifstream ini("shaderassist.ini");
    if(!ini.is_open()) {
        std::cout << "Failed to read .ini file" << std::endl;
        return 1;
    } else {
        parseIniFile(ini);
    }
    
    // Create a spirv directory for generated output spirv results
    if(config.SPIRVOutputPath[0] != '/' && config.SPIRVOutputPath[0] != '\\' && config.SPIRVOutputPath[1] != ':') {
        fs::path spirvPath = fs::current_path().append(config.SPIRVOutputPath);
        fs::create_directory(spirvPath);
    } else {
        fs::create_directory(config.SPIRVOutputPath);
    }

    // Print introductory message
    std::cout << "ShaderAssist, 2018" << std::endl;
    std::cout << "Enter -h for the list of commands." << std::endl;
    
    // Wait a short duration before starting the thread job to give std output the time to write the introductory message (before thread spams std output)
    // A more proper way would be to use some mutex for shared cout access, but this works just as fine :)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Start a thread to check for shaders, keep main thread for processing additional user input
    std::thread watchShaderThread(watchShaders, currentPath);

    // Check for user input
    std::string line;
    while(std::getline(std::cin, line)) {
        if(line == "-h" || line == "-help" || line == "help") {
            std::cout << "commands: " << std::endl;
            std::cout << "-h|-help|help:        list of commands"       << std::endl;
            std::cout << "-q|-quit|quit|exit:   quit ShaderAssist"      << std::endl;
            std::cout << "-r|-recompile:        recompile all shaders"  << std::endl;
        }
        if(line == "-q" || line == "-quit" || line == "quit" || line == "exit") {
            sApplicationExit = true;
            break;
        }
        if(line == "-r" || line == "-recompile") {
            std::cout << "forcing recompile" << std::endl;
            sRecompile = true;
        }
    }
    
    // Exit
    watchShaderThread.join();
    return 0;
}