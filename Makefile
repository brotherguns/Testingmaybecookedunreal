# ============================================================================
# Theos Makefile — Dumper-7 iOS dylib
# ============================================================================
ARCHS = arm64
TARGET = iphone:clang:latest:14.0

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = Dumper7

# Source files
Dumper7_FILES = \
	Dumper/ios_main.mm \
	Dumper/Settings.cpp \
	Dumper/Engine/Private/OffsetFinder/OffsetFinder.cpp \
	Dumper/Engine/Private/OffsetFinder/Offsets.cpp \
	Dumper/Engine/Private/Unreal/NameArray.cpp \
	Dumper/Engine/Private/Unreal/ObjectArray.cpp \
	Dumper/Engine/Private/Unreal/UnrealObjects.cpp \
	Dumper/Engine/Private/Unreal/UnrealTypes.cpp \
	Dumper/Generator/Private/Generators/CppGenerator.cpp \
	Dumper/Generator/Private/Generators/DumpspaceGenerator.cpp \
	Dumper/Generator/Private/Generators/Generator.cpp \
	Dumper/Generator/Private/Generators/IDAMappingGenerator.cpp \
	Dumper/Generator/Private/Generators/MappingGenerator.cpp \
	Dumper/Generator/Private/HashStringTable.cpp \
	Dumper/Generator/Private/Managers/CollisionManager.cpp \
	Dumper/Generator/Private/Managers/DependencyManager.cpp \
	Dumper/Generator/Private/Managers/EnumManager.cpp \
	Dumper/Generator/Private/Managers/MemberManager.cpp \
	Dumper/Generator/Private/Managers/PackageManager.cpp \
	Dumper/Generator/Private/Managers/StructManager.cpp \
	Dumper/Generator/Private/Wrappers/EnumWrapper.cpp \
	Dumper/Generator/Private/Wrappers/MemberWrappers.cpp \
	Dumper/Generator/Private/Wrappers/StructWrapper.cpp \
	Dumper/Platform/Private/PlatformApple.cpp \
	Dumper/Utils/Dumpspace/DSGen.cpp \
	Dumper/Utils/Compression/zstd.c

Dumper7_CFLAGS = \
	-fobjc-arc \
	-std=c++23 \
	-I$(THEOS_PROJECT_DIR)/Dumper \
	-I$(THEOS_PROJECT_DIR)/Dumper/Engine/Public \
	-I$(THEOS_PROJECT_DIR)/Dumper/Engine/Private \
	-I$(THEOS_PROJECT_DIR)/Dumper/Generator/Public \
	-I$(THEOS_PROJECT_DIR)/Dumper/Generator/Private \
	-I$(THEOS_PROJECT_DIR)/Dumper/Platform/Public \
	-I$(THEOS_PROJECT_DIR)/Dumper/Platform/Private \
	-I$(THEOS_PROJECT_DIR)/Dumper/Utils \
	-I$(THEOS_PROJECT_DIR)/Dumper/Utils/Compression \
	-I$(THEOS_PROJECT_DIR)/Dumper/Utils/Dumpspace \
	-I$(THEOS_PROJECT_DIR)/Dumper/Utils/Encoding \
	-I$(THEOS_PROJECT_DIR)/Dumper/Utils/Json \
	-DTARGET_OS_IPHONE=1 \
	-Wno-microsoft-include \
	-Wno-unused-variable \
	-Wno-deprecated-declarations

Dumper7_CCFLAGS = $(Dumper7_CFLAGS)

# Link against system frameworks
Dumper7_LIBRARIES = 

Dumper7_FRAMEWORKS = Foundation

# Where to inject — change this to the target game's bundle ID
TWEAK_TARGET_PROCESS = com.epicgames.FortniteIOS

include $(THEOS_MAKE_PATH)/tweak.mk
