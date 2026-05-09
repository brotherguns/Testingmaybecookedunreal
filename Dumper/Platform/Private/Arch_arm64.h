#pragma once
#include <cstdint>

// ARM64 instruction analysis helpers.
// Replaces Arch_x86.h for Apple Silicon / iOS targets.
namespace Architecture_ARM64
{
    // -----------------------------------------------------------------------
    // Address validity
    // -----------------------------------------------------------------------

    // ARM64 virtual addresses use 48-bit (or 52-bit with LPA) canonical form.
    // Top byte must be 0x00 (user) or 0xFF (kernel). We only deal with user.
    inline bool IsValidVirtualAddress(const uintptr_t Address)
    {
        constexpr uint64_t TopByteMask = 0xFFull << 56;
        return (Address & TopByteMask) == 0x0;
    }
    inline bool IsValidVirtualAddress(const void* Address)
    {
        return IsValidVirtualAddress(reinterpret_cast<uintptr_t>(Address));
    }

    // -----------------------------------------------------------------------
    // B / BL unconditional branch (26-bit immediate, pc-relative, *4)
    //   Encoding: [31:26]=000101 (B) or 100101 (BL), [25:0]=imm26
    // -----------------------------------------------------------------------
    inline bool IsBranch(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        return (Insn & 0xFC000000u) == 0x14000000u   // B
            || (Insn & 0xFC000000u) == 0x94000000u;  // BL
    }

    inline uintptr_t ResolveBranch(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        int64_t Offset = static_cast<int64_t>(Insn & 0x03FFFFFFu);
        if (Offset & 0x02000000u)            // sign-extend 26-bit
            Offset |= static_cast<int64_t>(0xFFFFFFFFFE000000LL);
        return Address + static_cast<uintptr_t>(Offset * 4);
    }

    // -----------------------------------------------------------------------
    // ADRP  (page-relative, 21-bit immhi:immlo, page = PC & ~0xFFF)
    //   Encoding: [31]=1, [30:29]=immlo, [28:24]=10000, [23:5]=immhi, [4:0]=Rd
    // -----------------------------------------------------------------------
    inline bool IsADRP(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        return (Insn & 0x9F000000u) == 0x90000000u;
    }

    inline uintptr_t ResolveADRP(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        const uint32_t ImmLo = (Insn >> 29) & 0x3u;
        const uint32_t ImmHi = (Insn >> 5) & 0x7FFFFu;
        int64_t Imm = static_cast<int64_t>((ImmHi << 2) | ImmLo);
        if (Imm & (1LL << 20))               // sign-extend 21-bit
            Imm |= static_cast<int64_t>(0xFFFFFFFFFFE00000LL);
        const uintptr_t Page = Address & ~static_cast<uintptr_t>(0xFFF);
        return Page + static_cast<uintptr_t>(Imm << 12);
    }

    // -----------------------------------------------------------------------
    // ADD (immediate) — often follows ADRP to add the page offset
    //   Encoding: [31:23]=x0010001, [21:10]=imm12, [9:5]=Rn, [4:0]=Rd
    // -----------------------------------------------------------------------
    inline bool IsADDImmediate(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        return (Insn & 0xFF000000u) == 0x91000000u   // 64-bit ADD imm
            || (Insn & 0xFF000000u) == 0x11000000u;  // 32-bit ADD imm
    }

    inline uint32_t GetADDImmediateOffset(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        uint32_t Imm12 = (Insn >> 10) & 0xFFFu;
        const uint32_t Shift = (Insn >> 22) & 0x3u;
        if (Shift == 1) Imm12 <<= 12;
        return Imm12;
    }

    // -----------------------------------------------------------------------
    // LDR (unsigned offset) — often follows ADRP to load from a got slot
    //   Encoding: [31:30]=size, [29:27]=111, [26]=V=0, [25:24]=01, [21:10]=imm12, ...
    // -----------------------------------------------------------------------
    inline bool IsLDRUnsigned(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        // Match 64-bit LDR (unsigned offset): 1111100101...
        return (Insn & 0xFFC00000u) == 0xF9400000u;
    }

    inline uint32_t GetLDRUnsignedOffset(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        const uint32_t Imm12 = (Insn >> 10) & 0xFFFu;
        return Imm12 * 8;   // scaled by 8 for 64-bit
    }

    // -----------------------------------------------------------------------
    // Resolve an ADRP+ADD or ADRP+LDR pair at Address/Address+4
    //   Returns the effective target address (not dereferenced for LDR).
    // -----------------------------------------------------------------------
    inline uintptr_t ResolveADRPPair(const uintptr_t Address)
    {
        if (!IsADRP(Address)) return 0;
        const uintptr_t Page = ResolveADRP(Address);
        const uintptr_t Next = Address + 4;

        if (IsADDImmediate(Next))
            return Page + GetADDImmediateOffset(Next);
        if (IsLDRUnsigned(Next))
            return Page + GetLDRUnsignedOffset(Next);
        return Page;
    }

    // -----------------------------------------------------------------------
    // BL-specific helpers (BL = Branch with Link, i.e. call instruction)
    //   Encoding: [31:26]=100101, [25:0]=imm26
    // -----------------------------------------------------------------------
    inline bool IsBL(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        return (Insn & 0xFC000000u) == 0x94000000u;
    }

    inline uintptr_t ResolveBL(const uintptr_t Address)
    {
        return ResolveBranch(Address);   // same encoding, just BL bit set
    }

    // -----------------------------------------------------------------------
    // FindNextFunctionStart — scan forward for a function prologue
    // -----------------------------------------------------------------------
    inline uintptr_t FindNextFunctionStart(const void* Address)
    {
        const uintptr_t Base = reinterpret_cast<uintptr_t>(Address);
        for (int i = 4; i < 0x1000; i += 4)
        {
            const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Base + i);
            // STP x29, x30, [sp, ...]   (common ARM64 prologue)
            if ((Insn & 0xFFC07FFF) == 0xA9007BFD)
                return Base + i;
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // RET detection
    // -----------------------------------------------------------------------
    inline bool IsFunctionRet(const uintptr_t Address)
    {
        const uint32_t Insn = *reinterpret_cast<const uint32_t*>(Address);
        return Insn == 0xD65F03C0u;   // RET (uses x30)
    }

    // -----------------------------------------------------------------------
    // Resolve a branch / jump if the instruction IS a branch, else return 0
    // -----------------------------------------------------------------------
    inline uintptr_t ResolveJumpIfInstructionIsJump(const uintptr_t Address, const uintptr_t Default = 0)
    {
        if (IsBranch(Address))
            return ResolveBranch(Address);
        return Default;
    }
}
