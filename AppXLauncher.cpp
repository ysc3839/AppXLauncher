#include "pch.h"
#include "AppXLauncher.h"

std::wstring LoadUtf8FileToUtf16(LPCWSTR fileName)
{
	wil::unique_hfile hFile(CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	THROW_LAST_ERROR_IF(!hFile);

	LARGE_INTEGER size;
	THROW_IF_WIN32_BOOL_FALSE(GetFileSizeEx(hFile.get(), &size));

	if (size.QuadPart == 0)
		return {};

	if constexpr (sizeof(size.QuadPart) > sizeof(size_t))
		THROW_HR_IF(E_INVALIDARG, size.QuadPart > SIZE_MAX);

	wil::unique_handle hFileMapping(CreateFileMappingW(hFile.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
	THROW_LAST_ERROR_IF(!hFileMapping);

	wil::unique_mapview_ptr<const char> view(reinterpret_cast<const char*>(MapViewOfFile(hFileMapping.get(), FILE_MAP_READ, 0, 0, 0)));
	THROW_LAST_ERROR_IF(!view);

	auto ptr = view.get();
	if (size.QuadPart >= 3 && ptr[0] == 0xEF && ptr[1] == 0xBB && ptr[2] == 0xBF) // Skip UTF-8 BOM
	{
		ptr += 3;
		size.QuadPart -= 3;
	}

	return Utf8ToUtf16({ ptr, static_cast<size_t>(size.QuadPart) });
}

auto ReadPackageConfig()
{
	auto path = g_exePath;
	auto str = LoadUtf8FileToUtf16(path.replace_filename(CONFIG_NAME).c_str());
	THROW_HR_IF(E_INVALIDARG, str.empty());

	auto json = winrt::Windows::Data::Json::JsonObject::Parse(str);
	auto packageFamilyName = json.GetNamedString(L"PackageFamilyName");
	auto appId = json.GetNamedString(L"AppId");

	return std::make_pair(packageFamilyName, appId);
}

auto ReadDllConfig()
{
	auto path = g_exePath;
	auto str = LoadUtf8FileToUtf16(path.replace_filename(CONFIG_NAME).c_str());
	THROW_HR_IF(E_INVALIDARG, str.empty());

	auto json = winrt::Windows::Data::Json::JsonObject::Parse(str);
	return json.GetNamedString(L"InjectDll");
}

auto FindFirstPackage(const winrt::param::hstring& packageFamilyName)
{
	return winrt::Windows::Management::Deployment::PackageManager().FindPackagesForUser({}, packageFamilyName).First().Current();
}

DWORD ActivateApplication(LPCWSTR appUserModelId)
{
	auto aam = wil::CoCreateInstance<IApplicationActivationManager>(CLSID_ApplicationActivationManager, CLSCTX_LOCAL_SERVER);

	THROW_IF_FAILED(CoAllowSetForegroundWindow(aam.get(), nullptr));

	DWORD pid = 0;
	THROW_IF_FAILED(aam->ActivateApplication(appUserModelId, nullptr, AO_NONE, &pid));

	return pid;
}

void InjectDll(HANDLE hProcess, std::wstring_view dllPath)
{
	SIZE_T bytes = dllPath.size() * sizeof(wchar_t);
	auto mem = RAIIVirtualAllocEx(hProcess, nullptr, bytes + sizeof(wchar_t), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	THROW_LAST_ERROR_IF(!mem);

	THROW_IF_WIN32_BOOL_FALSE(WriteProcessMemory(hProcess, mem.get(), dllPath.data(), bytes, nullptr));

	wil::unique_process_handle hThread(CreateRemoteThread(hProcess, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryW), mem.get(), 0, nullptr));
	THROW_LAST_ERROR_IF(!hThread);

	WaitForSingleObject(hThread.get(), 10000);
}

int APIENTRY wWinMain([[maybe_unused]] _In_ HINSTANCE hInstance,
	[[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance,
	[[maybe_unused]] _In_ LPWSTR    lpCmdLine,
	[[maybe_unused]] _In_ int       nCmdShow)
{
	g_exePath = GetModuleFsPath(nullptr);

	const int argc = __argc;
	if (argc < 5)
	{
		winrt::init_apartment();

		auto [packageFamilyName, appId] = ReadPackageConfig();

		auto package = FindFirstPackage(packageFamilyName);
		auto packageFullName = package.Id().FullName();

		auto debugSettings = wil::CoCreateInstance<IPackageDebugSettings>(CLSID_PackageDebugSettings);
		debugSettings->TerminateAllProcesses(packageFullName.c_str());
		debugSettings->DisableDebugging(packageFullName.c_str());
		debugSettings->EnableDebugging(packageFullName.c_str(), g_exePath.c_str(), nullptr);

		std::wstring appUserModelId;
		appUserModelId.reserve(static_cast<size_t>(packageFamilyName.size()) + 1 + appId.size());
		appUserModelId.assign(packageFamilyName);
		appUserModelId += L'!';
		appUserModelId += appId;
		ActivateApplication(appUserModelId.c_str());

		debugSettings->DisableDebugging(packageFullName.c_str());
	}
	else
	{
		DWORD pid = 0, tid = 0;
		wchar_t** argv = __wargv;
		for (int i = 1; i < argc - 1; ++i)
		{
			std::wstring_view arg = argv[i];
			if (arg == L"-p") // Process ID
			{
				pid = wcstoul(argv[++i], nullptr, 10);
			}
			else if (arg == L"-tid") // Thread ID
			{
				tid = wcstoul(argv[++i], nullptr, 10);
			}
		}

		if (pid != 0 && tid != 0)
		{
			fs::path injectDll = static_cast<std::wstring_view>(ReadDllConfig());

			wil::unique_process_handle hProcess(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
			THROW_LAST_ERROR_IF(!hProcess);

			auto path = injectDll;
			if (injectDll.is_relative())
			{
				path = g_exePath;
				path.replace_filename(injectDll);
			}
			InjectDll(hProcess.get(), path.native());

			wil::unique_process_handle hThread(OpenThread(THREAD_SUSPEND_RESUME, FALSE, tid));
			THROW_LAST_ERROR_IF(!hThread);

			ResumeThread(hThread.get());
		}
	}

	return 0;
}
