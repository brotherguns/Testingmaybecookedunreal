#pragma once

// ============================================================================
//  PlatformApple.h  —  Mach-O / Darwin platform layer for Dumper-7
//  Replaces PlatformWindows.h on iOS / macOS (Apple Silicon, ARM64)
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include "Settings.h"

// ---------------------------------------------------------------------------
// Opaque section handle (same layout as on Windows: 16 bytes)
// Internal layout: { base: uintptr_t, size: uint32_t, _pad: uint32_t }
// ---------------------------------------------------------------------------
struct SectionInfo
{
private:
    uint8_t Data[0x10] = { 0x0 };

public:
    SectionInfo() = delete;

    inline bool IsValid() const
    {
        for (int i = 0; i < static_cast<int>(sizeof(Data)); i++)
            if (Data[i] != 0x0)
                return true;
        return false;
    }
};

// ---------------------------------------------------------------------------
// Forward declarations so templates with default args can reference them
// ---------------------------------------------------------------------------
namespace PlatformApple
{
    template<typename T>
    T* FindAlignedValueInRange(const T Value, const int32_t Alignment, uintptr_t StartAddress, uint32_t Range);

    template<typename T>
    T* FindAlignedValueInSection(const SectionInfo& Info, T Value, const int32_t Alignment);

    template<typename T>
    T* FindAlignedValueInAllSections(
        const T Value,
        const int32_t Alignment = alignof(T),
        const uintptr_t StartAddress = 0x0,
        int32_t Range = 0,
        const char* const ModuleName = Settings::General::DefaultModuleName);
}

// ---------------------------------------------------------------------------
// Private helper (gives template friends access to impl)
// ---------------------------------------------------------------------------
class ApplePrivateImplHelper
{
public:
    template<typename T>
    friend T* PlatformApple::FindAlignedValueInRange(const T, const int32_t, uintptr_t, uint32_t);

    template<typename T>
    friend T* PlatformApple::FindAlignedValueInSection(const SectionInfo&, T, const int32_t);

    template<typename T>
    friend T* PlatformApple::FindAlignedValueInAllSections(const T, const int32_t, const uintptr_t, int32_t, const char* const);

private:
    using ValueCompareFuncType = bool(*)(const void* Value, const void* Candidate);

    static void* FindAlignedValueInRangeImpl(
        const void* ValuePtr, ValueCompareFuncType Cmp,
        int32_t TypeSize, int32_t Alignment,
        uintptr_t StartAddress, uint32_t Range);

    static void* FindAlignedValueInSectionImpl(
        const SectionInfo& Info, const void* ValuePtr,
        ValueCompareFuncType Cmp, int32_t TypeSize, int32_t Alignment);

    static void* FindAlignedValueInAllSectionsImpl(
        const void* ValuePtr, ValueCompareFuncType Cmp,
        int32_t TypeSize, int32_t Alignment,
        uintptr_t StartAddress, int32_t Range,
        const char* const ModuleName);
};

// ---------------------------------------------------------------------------
// Main platform namespace
// ---------------------------------------------------------------------------
namespace PlatformApple
{
    // Always 64-bit on modern iOS/macOS
    consteval bool Is32Bit() { return false; }

    // ---- Module base -------------------------------------------------------
    uintptr_t GetModuleBase(const char* const ModuleName = Settings::General::DefaultModuleName);
    uintptr_t GetOffset(const uintptr_t Address, const char* const ModuleName = Settings::General::DefaultModuleName);
    uintptr_t GetOffset(const void* Address,     const char* const ModuleName = Settings::General::DefaultModuleName);

    // ---- Section iteration -------------------------------------------------
    SectionInfo GetSectionInfo(const std::string& SegmentDotSection, const char* const ModuleName = Settings::General::DefaultModuleName);
    void* IterateSectionWithCallback(const SectionInfo& Info, const std::function<bool(void*)>& Callback, uint32_t Granularity = 4, uint32_t OffsetFromEnd = 0);
    void* IterateAllSectionsWithCallback(const std::function<bool(void*)>& Callback, uint32_t Granularity = 4, uint32_t OffsetFromEnd = 0, const char* const ModuleName = Settings::General::DefaultModuleName);

    // ---- Address probes ----------------------------------------------------
    bool IsAddressInAnyModule(const uintptr_t Address);
    bool IsAddressInAnyModule(const void*     Address);
    bool IsAddressInProcessRange(const uintptr_t Address);
    bool IsAddressInProcessRange(const void*     Address);
    bool IsBadReadPtr(const uintptr_t Address);
    bool IsBadReadPtr(const void*     Address);

    // ---- Import/export lookup (Mach-O dyld) --------------------------------
    const void* GetAddressOfImportedFunction(const char* SearchModuleName, const char* ModuleToImportFrom, const char* SearchFunctionName);
    const void* GetAddressOfImportedFunctionFromAnyModule(const char* ModuleToImportFrom, const char* SearchFunctionName);
    const void* GetAddressOfExportedFunction(const char* SearchModuleName, const char* SearchFunctionName);

    // ---- VTable iteration --------------------------------------------------
    template<bool bShouldResolveBranches = true>
    std::pair<const void*, int32_t> IterateVTableFunctions(
        void** VTable,
        const std::function<bool(const uint8_t* Address, int32_t Index)>& Callback,
        int32_t NumFunctions = 0x150,
        int32_t OffsetFromStart = 0x0);

    // ---- Pattern scanning --------------------------------------------------
    void* FindPattern(const char* Signature, uint32_t Offset = 0, bool bSearchAllSections = false, uintptr_t StartAddress = 0, const char* const ModuleName = Settings::General::DefaultModuleName);
    void* FindPatternInRange(const char* Signature, const void* Start, uintptr_t Range, bool bRelative = false, uint32_t Offset = 0);
    void* FindPatternInRange(const char* Signature, uintptr_t Start, uintptr_t Range, bool bRelative = false, uint32_t Offset = 0);
    void* FindPatternInRange(std::vector<int>&& Signature, const void* Start, uintptr_t Range, bool bRelative = false, uint32_t Offset = 0, uint32_t SkipCount = 0);

    // ---- String-reference scanning (ADRP+ADD pattern on ARM64) ------------
    template<bool bCheckIfLeaIsStrPtr = false, typename CharType = char>
    void* FindByStringInAllSections(
        const CharType* RefStr,
        uintptr_t StartAddress = 0,
        int32_t Range = 0,
        bool bSearchOnlyExecutableSections = true,
        const char* const ModuleName = Settings::General::DefaultModuleName);

    template<bool bCheckIfLeaIsStrPtr, typename CharType>
    void* FindStringInRange(const CharType* RefStr, uintptr_t StartAddress, int32_t Range);

    // ---- Aligned value search (template bodies below) ----------------------
    template<typename T>
    T* FindAlignedValueInRange(const T Value, const int32_t Alignment, uintptr_t StartAddress, uint32_t Range)
    {
        auto Cmp = [](const void* V, const void* C) -> bool {
            return *static_cast<const T*>(V) == *static_cast<const T*>(C);
        };
        return static_cast<T*>(ApplePrivateImplHelper::FindAlignedValueInRangeImpl(&Value, Cmp, sizeof(Value), Alignment, StartAddress, Range));
    }

    template<typename T>
    T* FindAlignedValueInSection(const SectionInfo& Info, const T Value, const int32_t Alignment)
    {
        auto Cmp = [](const void* V, const void* C) -> bool {
            return *static_cast<const T*>(V) == *static_cast<const T*>(C);
        };
        return static_cast<T*>(ApplePrivateImplHelper::FindAlignedValueInSectionImpl(Info, &Value, Cmp, sizeof(Value), Alignment));
    }

    template<typename T>
    T* FindAlignedValueInAllSections(const T Value, const int32_t Alignment, const uintptr_t StartAddress, int32_t Range, const char* const ModuleName)
    {
        auto Cmp = [](const void* V, const void* C) -> bool {
            return *static_cast<const T*>(V) == *static_cast<const T*>(C);
        };
        return static_cast<T*>(ApplePrivateImplHelper::FindAlignedValueInAllSectionsImpl(&Value, Cmp, sizeof(Value), Alignment, StartAddress, Range, ModuleName));
    }

    template<typename T>
    std::vector<T*> FindAllAlignedValuesInProcess(
        const T Value,
        const int32_t Alignment = alignof(T),
        const uintptr_t StartAddress = 0,
        int32_t Range = 0,
        const char* const ModuleName = Settings::General::DefaultModuleName)
    {
        std::vector<T*> Ret;
        uintptr_t Last = StartAddress;
        while (T* Ptr = FindAlignedValueInAllSections(Value, Alignment, Last, Range, ModuleName))
        {
            Ret.push_back(Ptr);
            Last = (reinterpret_cast<uintptr_t>(Ptr) + sizeof(T) + Alignment - 1) & ~static_cast<uintptr_t>(Alignment - 1);
        }
        return Ret;
    }
}
