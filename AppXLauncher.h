#pragma once

#include "resource.h"

namespace fs = std::filesystem;
using winrt::Windows::Foundation::IAsyncOperation;

fs::path g_exePath;

constexpr auto CONFIG_NAME = L"AppXLauncher.json";

#include "Util.hpp"
