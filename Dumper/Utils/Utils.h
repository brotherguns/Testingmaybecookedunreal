#pragma once

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#endif

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>

#include "TmpUtils.h"

#include "Platform.h"


inline std::pair<uintptr_t, uintptr_t> GetImageBaseAndSize(const char* const ModuleName = Settings::General::DefaultModuleName)
{
#if defined(_WIN32) || defined(_WIN64)
	const uintptr_t ImageBase = Platform::GetModuleBase(ModuleName);
	const PIMAGE_NT_HEADERS NtHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(ImageBase + reinterpret_cast<PIMAGE_DOS_HEADER>(ImageBase)->e_lfanew);

	return { ImageBase, NtHeader->OptionalHeader.SizeOfImage };
#elif defined(__APPLE__)
	#include <mach-o/dyld.h>
	#include <mach-o/loader.h>

	const uintptr_t ImageBase = Platform::GetModuleBase(ModuleName);
	const mach_header_64* Hdr = reinterpret_cast<const mach_header_64*>(ImageBase);

	/* Compute image size by summing all segment vmsize */
	uintptr_t TotalSize = 0;
	const uint8_t* Cursor = reinterpret_cast<const uint8_t*>(Hdr) + sizeof(mach_header_64);
	for (uint32_t i = 0; i < Hdr->ncmds; i++)
	{
		const load_command* LC = reinterpret_cast<const load_command*>(Cursor);
		if (LC->cmd == LC_SEGMENT_64)
		{
			const segment_command_64* Seg = reinterpret_cast<const segment_command_64*>(LC);
			TotalSize += Seg->vmsize;
		}
		Cursor += LC->cmdsize;
	}
	return { ImageBase, TotalSize };
#else
	return { Platform::GetModuleBase(ModuleName), 0 };
#endif
}


template<typename Type = const char*>
inline void* FindUnrealExecFunctionByString(Type RefStr, void* StartAddress = nullptr)
{
	const auto [ImageBase, ImageSize] = GetImageBaseAndSize();

	uint8_t* SearchStart = StartAddress ? reinterpret_cast<uint8_t*>(StartAddress) : reinterpret_cast<uint8_t*>(ImageBase);
	uint32_t SearchRange = static_cast<uint32_t>(ImageSize);

	const int32_t RefStrLen = StrlenHelper(RefStr);

#if defined(_WIN32) || defined(_WIN64)
	static auto IsValidExecFunctionNotSetupFunc = [](uintptr_t Address) -> bool
	{
		/* 
		* UFuntion construction functions setting up exec functions always start with these asm instructions:
		* sub rsp, 28h
		* 
		* In opcode bytes: 48 83 EC 28
		*/
		if (*reinterpret_cast<int32_t*>(Address) == 0x284883EC || *reinterpret_cast<int32_t*>(Address) == 0x4883EC28)
			return false;

		const void* SigOccurence = Platform::FindPatternInRange("48 8B 05 ? ? ? ? 48 85 C0 75 ? 48 8D 15", Address, 0x28);
		
		/* A signature specifically made for UFunctions-construction functions. If this signature is found we're in a function that we *don't* want. */
		return SigOccurence == nullptr;
	};

	for (uintptr_t i = 0; i < (SearchRange - 0x8); i += sizeof(void*))
	{
		const uintptr_t PossibleStringAddress = *reinterpret_cast<uintptr_t*>(SearchStart + i);
		const uintptr_t PossibleExecFuncAddress = *reinterpret_cast<uintptr_t*>(SearchStart + i + sizeof(void*));

		if (PossibleStringAddress == PossibleExecFuncAddress)
			continue;

		if (!Platform::IsAddressInProcessRange(PossibleStringAddress) || !Platform::IsAddressInProcessRange(PossibleExecFuncAddress))
			continue;

		if (Platform::IsBadReadPtr(reinterpret_cast<const void*>(PossibleStringAddress)) ||
			Platform::IsBadReadPtr(reinterpret_cast<const void*>(PossibleExecFuncAddress)))
			continue;

		if (!StrCmpHelper(RefStr, reinterpret_cast<const Type>(PossibleStringAddress), RefStrLen))
			continue;

		if (!IsValidExecFunctionNotSetupFunc(PossibleExecFuncAddress))
			continue;

		return reinterpret_cast<void*>(PossibleExecFuncAddress);
	}
#elif defined(__APPLE__)
	/*
	 * On ARM64 / iOS, exec function references are not stored as pointer pairs
	 * in a vtable-like structure. Instead, we use ADRP-based string scanning
	 * to find the instruction that loads RefStr, then look for the nearest BL
	 * to get the exec function.
	 */
	const void* StringRef = Platform::FindByStringInAllSections(RefStr, 0x0, 0x0, Settings::General::bSearchOnlyExecutableSectionsForStrings);

	if (!StringRef)
		return nullptr;

	const uintptr_t Base = reinterpret_cast<uintptr_t>(StringRef);

	/* Scan forward for a BL instruction that calls the exec function */
	for (int i = 0; i < 0x80; i += 4)
	{
		const uintptr_t Addr = Base + i;
		const uint32_t Instr = *reinterpret_cast<const uint32_t*>(Addr);

		/* BL = 1001 01 imm26 */
		if ((Instr & 0xFC000000) == 0x94000000)
		{
			const int32_t Imm26 = static_cast<int32_t>((Instr & 0x03FFFFFF) << 6) >> 6;
			const uintptr_t Target = Addr + static_cast<uintptr_t>(Imm26 * 4);

			if (Platform::IsAddressInProcessRange(Target))
				return reinterpret_cast<void*>(Target);
		}
	}
#endif

	return nullptr;
}
