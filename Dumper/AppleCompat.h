#pragma once

// ---------------------------------------------------------------------------
// AppleCompat.h  —  MSVC integer keyword shims for Apple Clang (iOS / macOS)
//
// Enums.h (and other UE4 SDK headers) use __int8/__int16/__int32/__int64 as
// type names, which are MSVC-specific keywords normally unlocked by
// -fms-extensions.  We cannot use -fms-extensions on Apple because it
// redefines va_list in a way that conflicts with <sys/_types/_va_list.h>.
//
// Solution: define them as typedefs before any UE4 header sees them.
// This header is force-included via -include for all Apple Clang TUs.
// ---------------------------------------------------------------------------

#ifdef __APPLE__
#include <cstdint>

typedef int8_t   __int8;
typedef int16_t  __int16;
typedef int32_t  __int32;
typedef int64_t  __int64;
#endif // __APPLE__
