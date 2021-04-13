#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX
// Windows Header Files
#include <windows.h>
#include <shobjidl.h>
#include <winternl.h>

// C RunTime Header Files
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <string>
