// ============================================================================
//  PlatformApple.cpp  —  Mach-O / Darwin platform layer for Dumper-7
// ============================================================================

#include "PlatformApple.h"
#include "Arch_arm64.h"
#include "TmpUtils.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/vm_region.h>
#include <dlfcn.h>
#include <string.h>
#include <algorithm>
#include <bit>

// ============================================================================
//  Internal helpers
// ============================================================================
namespace
{
    using namespace PlatformApple;

    // -------------------------------------------------------------------------
    // Mach-O section handle — same byte budget as SectionInfo (16 bytes)
    // -------------------------------------------------------------------------
    struct AppleSectionInfo
    {
        uintptr_t BaseAddress = 0;  // section vm start (already slid)
        uint32_t  Size        = 0;
        uint32_t  _pad        = 0;
    };
    static_assert(sizeof(AppleSectionInfo) == sizeof(SectionInfo));

    inline AppleSectionInfo SIToApple(const SectionInfo& S)
    {
        return std::bit_cast<AppleSectionInfo>(S);
    }
    inline SectionInfo AppleToSI(const AppleSectionInfo& A)
    {
        return std::bit_cast<SectionInfo>(A);
    }

    // -------------------------------------------------------------------------
    // Locate the image index for a given module name.
    // Pass nullptr to get the main executable (image 0).
    // -------------------------------------------------------------------------
    static int FindImageIndex(const char* ModuleName)
    {
        const uint32_t Count = _dyld_image_count();
        if (ModuleName == nullptr)
            return 0;   // main executable is always image 0

        const std::string LcName = Utils::StrToLower(ModuleName);
        for (uint32_t i = 0; i < Count; i++)
        {
            const char* ImageName = _dyld_get_image_name(i);
            if (!ImageName) continue;
            if (Utils::StrToLower(ImageName).find(LcName) != std::string::npos)
                return static_cast<int>(i);
        }
        return -1;
    }

    // -------------------------------------------------------------------------
    // Get image header + ASLR slide for a given module name
    // -------------------------------------------------------------------------
    static const mach_header_64* GetMachHeader(const char* const ModuleName, intptr_t* OutSlide = nullptr)
    {
        const int Idx = FindImageIndex(ModuleName);
        if (Idx < 0) return nullptr;

        const mach_header* Hdr = _dyld_get_image_header(static_cast<uint32_t>(Idx));
        if (OutSlide)
            *OutSlide = _dyld_get_image_vmaddr_slide(static_cast<uint32_t>(Idx));

        return reinterpret_cast<const mach_header_64*>(Hdr);
    }

    // -------------------------------------------------------------------------
    // Iterate every section in an image, calling Callback(sectaddr, sectsize, flagsvm).
    // Return true from Callback to stop iteration.
    // -------------------------------------------------------------------------
    static void IterateSections(
        const mach_header_64* Hdr,
        intptr_t Slide,
        const std::function<bool(uintptr_t Addr, uint64_t Size, uint32_t Flags, const char* SegName, const char* SectName)>& Callback)
    {
        if (!Hdr) return;

        const uint8_t* Cursor = reinterpret_cast<const uint8_t*>(Hdr) + sizeof(mach_header_64);
        for (uint32_t Cmd = 0; Cmd < Hdr->ncmds; Cmd++)
        {
            const load_command* LC = reinterpret_cast<const load_command*>(Cursor);
            if (LC->cmd == LC_SEGMENT_64)
            {
                const segment_command_64* Seg = reinterpret_cast<const segment_command_64*>(LC);
                const section_64* Sects = reinterpret_cast<const section_64*>(Seg + 1);
                for (uint32_t S = 0; S < Seg->nsects; S++)
                {
                    const uintptr_t Addr = static_cast<uintptr_t>(Sects[S].addr) + static_cast<uintptr_t>(Slide);
                    const uint64_t  Size = Sects[S].size;
                    if (Size == 0) { continue; }
                    if (Callback(Addr, Size, Sects[S].flags, Seg->segname, Sects[S].sectname))
                        return;
                }
            }
            Cursor += LC->cmdsize;
        }
    }

    // -------------------------------------------------------------------------
    // vm_region-based readable-memory check (no POSIX VirtualQuery equivalent)
    // -------------------------------------------------------------------------
    static bool IsReadableAddress(const void* Address)
    {
        if (!Address) return false;
        vm_address_t    Addr  = reinterpret_cast<vm_address_t>(Address);
        vm_size_t       Size  = 0;
        vm_region_basic_info_data_64_t Info;
        mach_msg_type_number_t InfoCount = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t ObjectName;
        kern_return_t Kr = vm_region_64(
            mach_task_self(), &Addr, &Size,
            VM_REGION_BASIC_INFO_64,
            reinterpret_cast<vm_region_info_t>(&Info),
            &InfoCount, &ObjectName);
        if (Kr != KERN_SUCCESS) return false;
        return (Info.protection & VM_PROT_READ) != 0;
    }

    static int64_t GetAlignedSizeWithOffsetFromEnd(uint32_t SizeToAlign, uint32_t Alignment, uint32_t OffsetFromEnd)
    {
        const uint32_t ValueToAlign = SizeToAlign - (Alignment - 1) - OffsetFromEnd;
        if (ValueToAlign > SizeToAlign) return -1;
        return Align(ValueToAlign, static_cast<uint32_t>(Alignment));
    }
}


// ============================================================================
//  ApplePrivateImplHelper implementations
// ============================================================================

void* ApplePrivateImplHelper::FindAlignedValueInRangeImpl(
    const void* ValuePtr, ValueCompareFuncType Cmp,
    int32_t TypeSize, int32_t Alignment,
    uintptr_t StartAddress, uint32_t Range)
{
    const int64_t End = GetAlignedSizeWithOffsetFromEnd(Range, Alignment, TypeSize);
    if (End < 0) return nullptr;
    for (int64_t i = 0; i <= End; i += Alignment)
    {
        void* Candidate = reinterpret_cast<void*>(StartAddress + i);
        if (Cmp(ValuePtr, Candidate))
            return Candidate;
    }
    return nullptr;
}

void* ApplePrivateImplHelper::FindAlignedValueInSectionImpl(
    const SectionInfo& Info, const void* ValuePtr,
    ValueCompareFuncType Cmp, int32_t TypeSize, int32_t Alignment)
{
    const AppleSectionInfo ASI = SIToApple(Info);
    return FindAlignedValueInRangeImpl(ValuePtr, Cmp, TypeSize, Alignment, ASI.BaseAddress, ASI.Size);
}

void* ApplePrivateImplHelper::FindAlignedValueInAllSectionsImpl(
    const void* ValuePtr, ValueCompareFuncType Cmp,
    int32_t TypeSize, int32_t Alignment,
    uintptr_t StartAddress, int32_t Range,
    const char* const ModuleName)
{
    intptr_t Slide = 0;
    const mach_header_64* Hdr = GetMachHeader(ModuleName, &Slide);
    if (!Hdr) return nullptr;

    void* Result = nullptr;
    IterateSections(Hdr, Slide,
        [&](uintptr_t Addr, uint64_t Size, uint32_t /*Flags*/, const char*, const char*) -> bool
        {
            if (StartAddress && (StartAddress >= Addr + Size)) return false;
            const uintptr_t SearchStart = (StartAddress > Addr) ? StartAddress : Addr;
            const uint32_t  SearchRange = static_cast<uint32_t>(Addr + Size - SearchStart);
            Result = FindAlignedValueInRangeImpl(ValuePtr, Cmp, TypeSize, Alignment, SearchStart, SearchRange);
            return Result != nullptr;
        });
    return Result;
}


// ============================================================================
//  PlatformApple — public API
// ============================================================================

uintptr_t PlatformApple::GetModuleBase(const char* const ModuleName)
{
    const mach_header_64* Hdr = GetMachHeader(ModuleName, nullptr);
    return reinterpret_cast<uintptr_t>(Hdr);
}

uintptr_t PlatformApple::GetOffset(const uintptr_t Address, const char* const ModuleName)
{
    return Address - GetModuleBase(ModuleName);
}
uintptr_t PlatformApple::GetOffset(const void* Address, const char* const ModuleName)
{
    return GetOffset(reinterpret_cast<uintptr_t>(Address), ModuleName);
}

// ---------------------------------------------------------------------------
// GetSectionInfo — SegmentDotSection format: "__TEXT.__text"  or just  "__text"
// ---------------------------------------------------------------------------
SectionInfo PlatformApple::GetSectionInfo(const std::string& SegmentDotSection, const char* const ModuleName)
{
    // Split on '.' if present
    std::string WantSeg, WantSect;
    const size_t Dot = SegmentDotSection.find('.');
    if (Dot != std::string::npos)
    {
        WantSeg  = SegmentDotSection.substr(0, Dot);
        WantSect = SegmentDotSection.substr(Dot + 1);
    }
    else
    {
        WantSect = SegmentDotSection;
    }

    intptr_t Slide = 0;
    const mach_header_64* Hdr = GetMachHeader(ModuleName, &Slide);
    if (!Hdr) return AppleToSI({ 0, 0, 0 });

    AppleSectionInfo Result = { 0, 0, 0 };
    IterateSections(Hdr, Slide,
        [&](uintptr_t Addr, uint64_t Size, uint32_t, const char* SegName, const char* SectName) -> bool
        {
            // Compare section name (strip leading underscores for convenience)
            auto Strip = [](const char* N) -> std::string {
                std::string S(N);
                // Mach-O section names like "__text" — keep as-is
                return S;
            };
            const bool SegMatch  = WantSeg.empty()  || Strip(SegName)  == WantSeg;
            const bool SectMatch = WantSect.empty() || Strip(SectName) == WantSect;
            if (SegMatch && SectMatch)
            {
                Result = { Addr, static_cast<uint32_t>(Size), 0 };
                return true;
            }
            return false;
        });
    return AppleToSI(Result);
}

// ---------------------------------------------------------------------------
void* PlatformApple::IterateSectionWithCallback(
    const SectionInfo& Info, const std::function<bool(void*)>& Callback,
    uint32_t Granularity, uint32_t OffsetFromEnd)
{
    const AppleSectionInfo ASI = SIToApple(Info);
    if (!ASI.BaseAddress || !ASI.Size) return nullptr;

    const int64_t IterSize = GetAlignedSizeWithOffsetFromEnd(ASI.Size, Granularity, OffsetFromEnd);
    if (IterSize < 0) return nullptr;

    for (uintptr_t Cur = ASI.BaseAddress; Cur < ASI.BaseAddress + static_cast<uintptr_t>(IterSize); Cur += Granularity)
    {
        if (Callback(reinterpret_cast<void*>(Cur)))
            return reinterpret_cast<void*>(Cur);
    }
    return nullptr;
}

void* PlatformApple::IterateAllSectionsWithCallback(
    const std::function<bool(void*)>& Callback,
    uint32_t Granularity, uint32_t OffsetFromEnd,
    const char* const ModuleName)
{
    intptr_t Slide = 0;
    const mach_header_64* Hdr = GetMachHeader(ModuleName, &Slide);
    if (!Hdr) return nullptr;

    void* Result = nullptr;
    IterateSections(Hdr, Slide,
        [&](uintptr_t Addr, uint64_t Size, uint32_t, const char*, const char*) -> bool
        {
            AppleSectionInfo Tmp = { Addr, static_cast<uint32_t>(Size), 0 };
            SectionInfo SI = AppleToSI(Tmp);
            if (void* Res = IterateSectionWithCallback(SI, Callback, Granularity, OffsetFromEnd))
            {
                Result = Res;
                return true;
            }
            return false;
        });
    return Result;
}

// ---------------------------------------------------------------------------
bool PlatformApple::IsAddressInAnyModule(const uintptr_t Address)
{
    return IsAddressInAnyModule(reinterpret_cast<const void*>(Address));
}
bool PlatformApple::IsAddressInAnyModule(const void* Address)
{
    const uint32_t Count = _dyld_image_count();
    for (uint32_t i = 0; i < Count; i++)
    {
        const mach_header_64* Hdr = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        if (!Hdr) continue;
        const intptr_t Slide = _dyld_get_image_vmaddr_slide(i);
        bool Found = false;
        IterateSections(Hdr, Slide,
            [&](uintptr_t Addr, uint64_t Size, uint32_t, const char*, const char*) -> bool {
                if (reinterpret_cast<uintptr_t>(Address) >= Addr &&
                    reinterpret_cast<uintptr_t>(Address) <  Addr + Size)
                {
                    Found = true;
                    return true;
                }
                return false;
            });
        if (Found) return true;
    }
    return false;
}

bool PlatformApple::IsAddressInProcessRange(const uintptr_t Address)
{
    return IsAddressInAnyModule(Address);
}
bool PlatformApple::IsAddressInProcessRange(const void* Address)
{
    return IsAddressInAnyModule(Address);
}

bool PlatformApple::IsBadReadPtr(const uintptr_t Address)
{
    return IsBadReadPtr(reinterpret_cast<const void*>(Address));
}
bool PlatformApple::IsBadReadPtr(const void* Address)
{
    if (!Address) return true;
    if (!Architecture_ARM64::IsValidVirtualAddress(reinterpret_cast<uintptr_t>(Address)))
        return true;
    return !IsReadableAddress(Address);
}

// ---------------------------------------------------------------------------
// Export/Import lookup via dyld / dl APIs
// ---------------------------------------------------------------------------
const void* PlatformApple::GetAddressOfExportedFunction(const char* SearchModuleName, const char* SearchFunctionName)
{
    // Try dlopen first to get a handle
    const int Idx = FindImageIndex(SearchModuleName);
    if (Idx < 0) return nullptr;
    const char* FullPath = _dyld_get_image_name(static_cast<uint32_t>(Idx));
    void* Handle = dlopen(FullPath, RTLD_NOLOAD | RTLD_NOW);
    if (!Handle) return nullptr;
    const void* Sym = dlsym(Handle, SearchFunctionName);
    dlclose(Handle);
    return Sym;
}

const void* PlatformApple::GetAddressOfImportedFunction(const char* SearchModuleName, const char* /*ModuleToImportFrom*/, const char* SearchFunctionName)
{
    // On Mach-O the "import table" is the GOT/stubs — easiest is to just resolve via dlsym
    return GetAddressOfExportedFunction(SearchModuleName, SearchFunctionName);
}

const void* PlatformApple::GetAddressOfImportedFunctionFromAnyModule(const char* /*ModuleToImportFrom*/, const char* SearchFunctionName)
{
    return dlsym(RTLD_DEFAULT, SearchFunctionName);
}

// ---------------------------------------------------------------------------
// VTable iteration
// ---------------------------------------------------------------------------
template<bool bShouldResolveBranches>
std::pair<const void*, int32_t> PlatformApple::IterateVTableFunctions(
    void** VTable,
    const std::function<bool(const uint8_t* Address, int32_t Index)>& Callback,
    int32_t NumFunctions,
    int32_t OffsetFromStart)
{
    if (!Callback) return { nullptr, -1 };

    auto ResolveBranch = [](const void* FuncPtr) -> const uint8_t* {
        if constexpr (bShouldResolveBranches)
        {
            const uintptr_t Addr = reinterpret_cast<uintptr_t>(FuncPtr);
            if (Architecture_ARM64::IsBranch(Addr))
            {
                const uintptr_t Target = Architecture_ARM64::ResolveBranch(Addr);
                if (PlatformApple::IsAddressInProcessRange(Target))
                    return reinterpret_cast<const uint8_t*>(Target);
            }
        }
        return reinterpret_cast<const uint8_t*>(FuncPtr);
    };

    for (int i = OffsetFromStart; i < OffsetFromStart + NumFunctions; i++)
    {
        const uintptr_t FuncAddr = reinterpret_cast<uintptr_t>(VTable[i]);
        if (!FuncAddr || !IsAddressInProcessRange(FuncAddr)) break;

        const uint8_t* Resolved = ResolveBranch(reinterpret_cast<const void*>(FuncAddr));
        if (Callback(Resolved, i))
            return { Resolved, i };
    }
    return { nullptr, -1 };
}

// Explicit instantiations
template std::pair<const void*, int32_t> PlatformApple::IterateVTableFunctions<true>(void**, const std::function<bool(const uint8_t*, int32_t)>&, int32_t, int32_t);
template std::pair<const void*, int32_t> PlatformApple::IterateVTableFunctions<false>(void**, const std::function<bool(const uint8_t*, int32_t)>&, int32_t, int32_t);

// ---------------------------------------------------------------------------
// Pattern scanning
// ---------------------------------------------------------------------------
void* PlatformApple::FindPatternInRange(
    std::vector<int>&& Signature, const void* Start, uintptr_t Range,
    bool bRelative, uint32_t Offset, uint32_t SkipCount)
{
    const int64_t PatLen = static_cast<int64_t>(Signature.size());
    const int*    Bytes  = Signature.data();
    uint32_t      Skips  = 0;

    for (int64_t i = 0; i < static_cast<int64_t>(Range) - PatLen; i++)
    {
        bool bFound = true;
        for (int64_t j = 0; j < PatLen; j++)
        {
            if (Bytes[j] != -1 && static_cast<const uint8_t*>(Start)[i + j] != static_cast<uint8_t>(Bytes[j]))
            { bFound = false; break; }
        }
        if (!bFound) continue;
        if (Skips++ != SkipCount) continue;

        uintptr_t Address = reinterpret_cast<uintptr_t>(Start) + i;
        if (bRelative)
        {
            if (Offset == static_cast<uint32_t>(-1)) Offset = static_cast<uint32_t>(PatLen);
            Address = (Address + Offset + 4) + *reinterpret_cast<const int32_t*>(Address + Offset);
        }
        return reinterpret_cast<void*>(Address);
    }
    return nullptr;
}

static std::vector<int> PatternToByte(const char* Pattern)
{
    std::vector<int> Bytes;
    const char* End = Pattern + strlen(Pattern);
    for (const char* C = Pattern; C < End; ++C)
    {
        if (*C == '?') { if (*(C+1) == '?') ++C; Bytes.push_back(-1); }
        else { Bytes.push_back(static_cast<int>(strtoul(C, const_cast<char**>(&C), 16))); }
    }
    return Bytes;
}

void* PlatformApple::FindPatternInRange(const char* Signature, const void* Start, uintptr_t Range, bool bRelative, uint32_t Offset)
{
    return FindPatternInRange(PatternToByte(Signature), Start, Range, bRelative, Offset);
}
void* PlatformApple::FindPatternInRange(const char* Signature, uintptr_t Start, uintptr_t Range, bool bRelative, uint32_t Offset)
{
    return FindPatternInRange(Signature, reinterpret_cast<const void*>(Start), Range, bRelative, Offset);
}

void* PlatformApple::FindPattern(
    const char* Signature, uint32_t Offset,
    bool bSearchAllSections, uintptr_t StartAddress,
    const char* const ModuleName)
{
    intptr_t Slide = 0;
    const mach_header_64* Hdr = GetMachHeader(ModuleName, &Slide);
    if (!Hdr) return nullptr;

    void* Result = nullptr;
    IterateSections(Hdr, Slide,
        [&](uintptr_t Addr, uint64_t Size, uint32_t Flags, const char* SegName, const char*) -> bool
        {
            // If not searching all sections, only search __TEXT.__text
            if (!bSearchAllSections)
            {
                constexpr uint32_t S_ATTR_PURE_INSTRUCTIONS = 0x80000000;
                if (!(Flags & S_ATTR_PURE_INSTRUCTIONS) &&
                    std::string(SegName) != "__TEXT") return false;
            }
            if (StartAddress && StartAddress >= Addr + Size) return false;
            const uintptr_t SearchStart = (StartAddress > Addr) ? StartAddress : Addr;
            Result = FindPatternInRange(Signature, reinterpret_cast<const void*>(SearchStart), Addr + Size - SearchStart, Offset != 0, Offset);
            return Result != nullptr;
        });
    return Result;
}

// ---------------------------------------------------------------------------
// String reference scanning — ARM64 ADRP+ADD/LDR pattern
// ---------------------------------------------------------------------------
namespace
{
    template<typename CharType>
    inline int StrlenHelper(const CharType* S) {
        int N = 0; while (S[N]) N++; return N;
    }
    template<typename CharType>
    inline bool StrnCmpHelper(const CharType* A, const CharType* B, int N) {
        for (int i = 0; i < N; i++) if (A[i] != B[i]) return false;
        return true;
    }
}

template<bool bCheckIfIsStrPtr, typename CharType>
void* PlatformApple::FindStringInRange(const CharType* RefStr, uintptr_t StartAddress, int32_t Range)
{
    const uint8_t* Search   = reinterpret_cast<const uint8_t*>(StartAddress);
    const int32_t  RefLen   = StrlenHelper(RefStr) + 1;
    constexpr int32_t Guard = 8;   // don't read beyond end

    for (int32_t i = 0; i < Range - Guard; i += 4)  // ARM64: 4-byte aligned instructions
    {
        const uintptr_t Addr = StartAddress + i;

        // ADRP instruction
        if (!Architecture_ARM64::IsADRP(Addr)) continue;

        const uintptr_t Target = Architecture_ARM64::ResolveADRPPair(Addr);
        if (!Target || !IsAddressInProcessRange(Target)) continue;
        if (IsBadReadPtr(reinterpret_cast<const void*>(Target))) continue;

        if (StrnCmpHelper(RefStr, reinterpret_cast<const CharType*>(Target), RefLen))
            return reinterpret_cast<void*>(Addr);

        if constexpr (bCheckIfIsStrPtr)
        {
            const CharType* Inner = *reinterpret_cast<const CharType* const*>(Target);
            if (IsAddressInProcessRange(reinterpret_cast<uintptr_t>(Inner)) &&
                StrnCmpHelper(RefStr, Inner, RefLen))
                return reinterpret_cast<void*>(Addr);
        }
    }
    return nullptr;
}

template<bool bCheckIfIsStrPtr, typename CharType>
void* PlatformApple::FindByStringInAllSections(
    const CharType* RefStr, uintptr_t StartAddress, int32_t Range,
    bool bSearchOnlyExecutableSections, const char* const ModuleName)
{
    static_assert(std::is_same_v<CharType, char> || std::is_same_v<CharType, wchar_t>);

    intptr_t Slide = 0;
    const mach_header_64* Hdr = GetMachHeader(ModuleName, &Slide);
    if (!Hdr) return nullptr;

    void* Result = nullptr;
    IterateSections(Hdr, Slide,
        [&](uintptr_t Addr, uint64_t Size, uint32_t Flags, const char*, const char*) -> bool
        {
            if (bSearchOnlyExecutableSections)
            {
                constexpr uint32_t ExecFlag = 0x80000000; // S_ATTR_PURE_INSTRUCTIONS
                if (!(Flags & ExecFlag)) return false;
            }
            if (StartAddress && StartAddress >= Addr + Size) return false;
            const uintptr_t SearchStart = (StartAddress > Addr) ? StartAddress : Addr;
            const int32_t   SearchRange = static_cast<int32_t>(Addr + Size - SearchStart - 8);
            if (SearchRange <= 0) return false;
            if (Range > 0 && SearchRange > Range) return false;
            Result = FindStringInRange<bCheckIfIsStrPtr, CharType>(RefStr, SearchStart, SearchRange);
            return Result != nullptr;
        });
    return Result;
}

// Explicit instantiations to satisfy the linker
template void* PlatformApple::FindByStringInAllSections<false, char>   (const char*,    uintptr_t, int32_t, bool, const char* const);
template void* PlatformApple::FindByStringInAllSections<false, wchar_t>(const wchar_t*, uintptr_t, int32_t, bool, const char* const);
template void* PlatformApple::FindByStringInAllSections<true,  char>   (const char*,    uintptr_t, int32_t, bool, const char* const);
template void* PlatformApple::FindByStringInAllSections<true,  wchar_t>(const wchar_t*, uintptr_t, int32_t, bool, const char* const);

template void* PlatformApple::FindStringInRange<false, char>   (const char*,    uintptr_t, int32_t);
template void* PlatformApple::FindStringInRange<false, wchar_t>(const wchar_t*, uintptr_t, int32_t);
template void* PlatformApple::FindStringInRange<true,  char>   (const char*,    uintptr_t, int32_t);
template void* PlatformApple::FindStringInRange<true,  wchar_t>(const wchar_t*, uintptr_t, int32_t);
