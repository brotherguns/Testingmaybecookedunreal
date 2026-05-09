#include <chrono>
#include <fstream>
#include "Settings.h"

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#endif

#include <filesystem>
#include <string>

#include "Unreal/UnrealObjects.h"
#include "Unreal/ObjectArray.h"

void Settings::InitWeakObjectPtrSettings()
{
	const UEStruct LoadAsset = ObjectArray::FindObjectFast<UEFunction>("LoadAsset", EClassCastFlags::Function);

	if (!LoadAsset)
	{
		std::cerr << "\nDumper-7: 'LoadAsset' wasn't found, could not determine value for 'bIsWeakObjectPtrWithoutTag'!\n" << std::endl;
		return;
	}

	const UEProperty Asset = LoadAsset.FindMember("Asset", EClassCastFlags::SoftObjectProperty);
	if (!Asset)
	{
		std::cerr << "\nDumper-7: 'Asset' wasn't found, could not determine value for 'bIsWeakObjectPtrWithoutTag'!\n" << std::endl;
		return;
	}

	const UEStruct SoftObjectPath = ObjectArray::FindStructFast("SoftObjectPath");

	constexpr int32 SizeOfFFWeakObjectPtr = 0x08;
	constexpr int32 OldUnrealAssetPtrSize = 0x10;

	if (SoftObjectPath)
	{
		const int32 SoftObjectPathSize = SoftObjectPath.GetStructSize();
		Settings::Internal::bIsWeakObjectPtrWithoutTag = (Asset.GetSize() - SoftObjectPathSize) == SizeOfFFWeakObjectPtr;
	}
	else
	{
		Settings::Internal::bIsWeakObjectPtrWithoutTag = Asset.GetSize() == OldUnrealAssetPtrSize;
	}
}

void Settings::InitLargeWorldCoordinateSettings()
{
	const UEStruct Transform = ObjectArray::FindStructFast("Transform");

	if (!Transform)
	{
		std::cerr << "\nDumper-7: 'Transform' wasn't found, could not determine value for 'bUseLargeWorldCoordinates'!\n" << std::endl;
		return;
	}

	/* FTransform is 0x30 on float and 0x60 on double */
	Settings::Internal::bUseLargeWorldCoordinates = Transform.GetStructSize() == 0x60;
}

void Settings::InitObjectPtrPropertySettings()
{
	/* Search for FObjectPtrProperty in GObjects. It exists on UE5+ games. */
	for (UEObject Obj : ObjectArray())
	{
		if (!Obj.IsA(EClassCastFlags::Class))
			continue;

		UEClass ObjAsClass = Obj.Cast<UEClass>();

		if (!ObjAsClass.HasTypeFlag(EClassCastFlags::FieldPathProperty))
			continue;

		const std::string Name = ObjAsClass.GetName();

		Settings::Internal::bIsObjPtrInsteadOfFieldPathProperty = (Name == "ObjectPtrProperty");
		return;
	}
}

void Settings::InitArrayDimSizeSettings()
{
	const UEStruct Rotator = ObjectArray::FindStructFast("Rotator");

	if (!Rotator)
	{
		std::cerr << "\nDumper-7: 'Rotator' wasn't found, could not determine value for 'bUseUint8ArrayDim'!\n" << std::endl;
		return;
	}

	for (UEProperty Prop : Rotator.GetProperties())
	{
		constexpr int32 ArrayDimOffset_uint8  = Off::Property::ArrayDim;
		constexpr int32 ArrayDimOffset_int32  = Off::Property::ArrayDim;

		const uint8_t ArrayDimAsUint8 = *reinterpret_cast<const uint8_t* >(reinterpret_cast<const uint8_t*>(Prop.GetAddress()) + Off::Property::ArrayDim);
		const int32   ArrayDimAsInt32 = *reinterpret_cast<const int32*   >(reinterpret_cast<const uint8_t*>(Prop.GetAddress()) + Off::Property::ArrayDim);

		if (ArrayDimAsUint8 == 1 && ArrayDimAsInt32 != 1)
		{
			Settings::Internal::bUseUint8ArrayDim = true;
			return;
		}

		if (ArrayDimAsInt32 == 1)
		{
			Settings::Internal::bUseUint8ArrayDim = false;
			return;
		}
	}
}

void Settings::Config::Load()
{
	namespace fs = std::filesystem;

	// Try local Dumper-7.ini
	const std::string LocalPath = (fs::current_path() / "Dumper-7.ini").string();
	const char* ConfigPath = nullptr;

	if (fs::exists(LocalPath))
	{
		ConfigPath = LocalPath.c_str();
	}
	else if (fs::exists(GlobalConfigPath))
	{
		ConfigPath = GlobalConfigPath;
	}

	if (!ConfigPath)
		return;

	/* On iOS we don't have GetPrivateProfileStringA — use a simple key=value parser */
#if defined(__APPLE__)
	std::ifstream IniFile(ConfigPath);
	std::string Line;
	while (std::getline(IniFile, Line))
	{
		auto Sep = Line.find('=');
		if (Sep == std::string::npos) continue;
		std::string Key = Line.substr(0, Sep);
		std::string Val = Line.substr(Sep + 1);
		// Trim
		while (!Key.empty() && (Key.back() == ' ' || Key.back() == '\r')) Key.pop_back();
		while (!Val.empty() && (Val.front() == ' ')) Val.erase(Val.begin());
		while (!Val.empty() && (Val.back() == '\r' || Val.back() == '\n')) Val.pop_back();

		if (Key == "SDKNamespaceName")
			SDKNamespaceName = Val;
		else if (Key == "SDKGenerationPath")
			Settings::Generator::SDKGenerationPath = Val;
		else if (Key == "SleepTimeout")
			SleepTimeout = std::stoi(Val);
		else if (Key == "DumpKey")
			DumpKey = std::stoi(Val);
	}
#else
	char SDKNamespace[256] = {};
	GetPrivateProfileStringA("Settings", "SDKNamespaceName", "SDK", SDKNamespace, sizeof(SDKNamespace), ConfigPath);
	SDKNamespaceName = SDKNamespace;

	char SDKPath[256] = {};
	GetPrivateProfileStringA("Settings", "SDKGenerationPath", "C:/Dumper-7", SDKPath, sizeof(SDKPath), ConfigPath);
	if (strcmp(SDKPath, "C:/Dumper-7") != 0)
	{
		try
		{
			auto UserSDKPath = fs::path(SDKPath);
			if (!fs::exists(UserSDKPath))
				fs::create_directories(UserSDKPath);

			std::error_code ec;
			auto SDKCanonicalPath = fs::canonical(UserSDKPath, ec);
			if (!ec)
				Settings::Generator::SDKGenerationPath = SDKCanonicalPath.string();
		}
		catch (const std::filesystem::filesystem_error& fe)
		{
			std::cerr << "Invalid SDK Generation Path: " << fe.what() << std::endl;
		}
	}

	SleepTimeout = max(GetPrivateProfileIntA("Settings", "SleepTimeout", 0, ConfigPath), 0);
	if (SleepTimeout > 0 && SleepTimeout < 1000)
		SleepTimeout *= 1000;

	DumpKey = GetPrivateProfileIntA("Settings", "DumpKey", 0, ConfigPath);
#endif
}

void Settings::Config::DelayDumperStart()
{
	if (DumpKey == 0)
	{
#if defined(__APPLE__)
		if (SleepTimeout > 0) usleep(SleepTimeout * 1000);
#else
		Sleep(SleepTimeout);
#endif
		return;
	}

#if !defined(__APPLE__)
	const auto DelayStartTime = std::chrono::high_resolution_clock::now();

	while (true)
	{
		if (GetAsyncKeyState(DumpKey) & 0x8000)
			return;

		if (SleepTimeout > 0)
		{
			const auto Now = std::chrono::high_resolution_clock::now();
			const auto ElapsedTime = std::chrono::duration<double, std::milli>(Now - DelayStartTime);
			if (ElapsedTime.count() > SleepTimeout)
			{
				std::cerr << "Sleep Timeout exceeded, proceeding with dump...\n";
				return;
			}
		}

		Sleep(50);
	}
#endif
}
