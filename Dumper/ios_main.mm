// ============================================================================
// ios_main.mm  —  Dylib entry point for Dumper-7 on iOS / ARM64
//
// Replaces DllMain. The __attribute__((constructor)) fires when the dylib is
// loaded via DYLD_INSERT_LIBRARIES or a Cydia/Dopamine injection framework.
// The actual dump runs on a dedicated pthread so the loader thread is not blocked.
// ============================================================================

#import <Foundation/Foundation.h>

#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Generator/Public/Generators/Generator.h"
#include "Settings.h"
#include "Platform.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Output directory — writable regardless of jailbreak type
// ---------------------------------------------------------------------------
static const char* GetOutputDir()
{
    // Prefer app sandbox Documents so the user can access via Files.app
    @autoreleasepool {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if ([paths count] > 0)
        {
            static std::string Dir = std::string([[paths objectAtIndex:0] UTF8String]) + "/Dumper-7";
            return Dir.c_str();
        }
    }
    return "/var/mobile/Media/Dumper-7";
}

// ---------------------------------------------------------------------------
// Stderr redirect — write to a log file so you can 'cat' it after the dump
// ---------------------------------------------------------------------------
static void RedirectStderrToFile(const char* OutputDir)
{
    fs::create_directories(OutputDir);
    const std::string LogPath = std::string(OutputDir) + "/dumper7.log";
    freopen(LogPath.c_str(), "w", stderr);
}

// ---------------------------------------------------------------------------
// Main dump thread
// ---------------------------------------------------------------------------
static void* DumperThread(void*)
{
    const char* OutputDir = GetOutputDir();
    RedirectStderrToFile(OutputDir);

    std::cerr << "[Dumper-7] iOS port — thread started\n";
    std::cerr << "[Dumper-7] Output dir: " << OutputDir << "\n\n";

    // Give the game a moment to finish initializing UE internals
    sleep(5);

    // Override the SDK generation path to a writable location
    Settings::Generator::SDKGenerationPath = OutputDir;
    Settings::Config::Load();

    try
    {
        Generator::Run();
        std::cerr << "\n[Dumper-7] Dump complete! Files are in " << OutputDir << "\n";
    }
    catch (const std::exception& Ex)
    {
        std::cerr << "\n[Dumper-7] EXCEPTION: " << Ex.what() << "\n";
    }
    catch (...)
    {
        std::cerr << "\n[Dumper-7] UNKNOWN EXCEPTION during dump!\n";
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// Constructor — fires immediately when the dylib is loaded
// ---------------------------------------------------------------------------
__attribute__((constructor))
static void Dumper7Constructor()
{
    pthread_t Thread;
    pthread_attr_t Attr;
    pthread_attr_init(&Attr);
    pthread_attr_setdetachstate(&Attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&Thread, &Attr, DumperThread, nullptr);
    pthread_attr_destroy(&Attr);
}
