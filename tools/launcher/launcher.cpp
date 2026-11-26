// Starts cs2.exe and loads skinchanger.dll into it before the game runs.
//   launcher.exe                      -> uses saved path, asks on first run
//   launcher.exe --set "C:\...\cs2.exe"
//   launcher.exe --args "-novid"      -> extra CS2 options (saved)
//   launcher.exe --dll "C:\...\other.dll"
//   launcher.exe --no-inject          -> only start the game
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <tlhelp32.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kIniName[] = L"launcher.ini";
constexpr wchar_t kDllName[] = L"skinchanger.dll";
constexpr wchar_t kLogName[] = L"launcher.log";
constexpr wchar_t kTitle[] = L"CS2 Skin Changer launcher";

fs::path exe_dir() {
    wchar_t buf[1024]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, 1024);
    return n ? fs::path(buf).parent_path() : fs::current_path();
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

void log_line(const std::wstring& msg) {
    std::ofstream out(exe_dir() / kLogName, std::ios::app);
    SYSTEMTIME st{}; GetLocalTime(&st);
    char t[32]{};
    sprintf_s(t, "[%02u:%02u:%02u] ", st.wHour, st.wMinute, st.wSecond);
    out << t << narrow(msg) << "\n";
}

void fail(const std::wstring& msg) {
    log_line(L"ошибка: " + msg);
    MessageBoxW(nullptr, msg.c_str(), kTitle, MB_ICONERROR | MB_OK);
}

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !fs::is_directory(p, ec);
}

struct Settings {
    std::wstring exe_path;
    std::wstring dll_path;
    std::wstring args;
};

Settings load() {
    Settings s;
    std::ifstream in(exe_dir() / kIniName);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("exe_path=", 0) == 0) s.exe_path = widen(line.substr(9));
        else if (line.rfind("dll_path=", 0) == 0) s.dll_path = widen(line.substr(9));
        else if (line.rfind("args=", 0) == 0) s.args = widen(line.substr(5));
    }
    return s;
}

void save(const Settings& s) {
    std::ofstream out(exe_dir() / kIniName, std::ios::trunc);
    out << "exe_path=" << narrow(s.exe_path) << "\n";
    out << "dll_path=" << narrow(s.dll_path) << "\n";
    out << "args=" << narrow(s.args) << "\n";
}

std::wstring pick_file(const wchar_t* filter, const wchar_t* title) {
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::wstring(buf) : std::wstring();
}

bool steam_running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
    bool found = false;
    if (Process32FirstW(snap, &pe))
        do {
            if (_wcsicmp(pe.szExeFile, L"steam.exe") == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return found;
}

std::vector<DWORD> pids_named(const wchar_t* name) {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe))
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) out.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return out;
}

bool module_loaded(DWORD pid, const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{}; me.dwSize = sizeof me;
    bool found = false;
    if (Module32FirstW(snap, &me))
        do {
            if (_wcsicmp(me.szModule, name) == 0) { found = true; break; }
        } while (Module32NextW(snap, &me));
    CloseHandle(snap);
    return found;
}

bool inject_dll(HANDLE proc, const std::wstring& dll, std::wstring& err) {
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { err = L"VirtualAllocEx, код " + std::to_wstring(GetLastError()); return false; }
    if (!WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr)) {
        err = L"WriteProcessMemory, код " + std::to_wstring(GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return false;
    }
    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE th = CreateRemoteThread(proc, nullptr, 0, load, remote, 0, nullptr);
    if (!th) {
        err = L"CreateRemoteThread, код " + std::to_wstring(GetLastError());
        VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        return false;
    }
    WaitForSingleObject(th, 15000);
    DWORD rc = 0;
    GetExitCodeThread(th, &rc);
    CloseHandle(th);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    if (rc == 0) {
        err = L"LoadLibraryW вернул 0 (код " + std::to_wstring(GetLastError()) +
              L"). Чаще всего нет зависимостей: поставь VC++ Redist x64";
        return false;
    }
    return true;
}

// Launches suspended, injects, resumes. If the game re-executes itself, keeps
// topping up for 40 s while no copy of the DLL is loaded in any cs2.exe.
bool launch_and_inject(const std::wstring& exe, const std::wstring& args, const fs::path& dll,
                       bool& launched, std::wstring& err) {
    launched = false;
    std::wstring cmd = L"\"" + exe + L"\"";
    if (!args.empty()) cmd += L" " + args;
    std::wstring buf = cmd;
    buf.push_back(L'\0');

    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const fs::path cwd = fs::path(exe).parent_path();
    if (!CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        cwd.c_str(), &si, &pi)) {
        err = L"CreateProcess, код " + std::to_wstring(GetLastError());
        return false;
    }
    launched = true;
    log_line(L"cs2.exe создан (suspended), PID " + std::to_wstring(pi.dwProcessId));

    if (!dll.empty()) {
        std::wstring e;
        if (inject_dll(pi.hProcess, dll.wstring(), e)) {
            log_line(L"инъекция до старта: ok");
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
        log_line(L"инъекция до старта: " + e);
        err = e;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (dll.empty()) return true;

    const DWORD rights = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                         PROCESS_VM_WRITE | PROCESS_VM_READ;
    for (int i = 0; i < 40; ++i) {
        Sleep(1000);
        for (DWORD pid : pids_named(L"cs2.exe")) {
            if (module_loaded(pid, kDllName)) { log_line(L"DLL уже в PID " + std::to_wstring(pid)); return true; }
            HANDLE h = OpenProcess(rights, FALSE, pid);
            if (!h) continue;
            std::wstring e;
            const bool ok = inject_dll(h, dll.wstring(), e);
            CloseHandle(h);
            if (ok) { log_line(L"добор в PID " + std::to_wstring(pid) + L": ok"); return true; }
        }
    }
    err = L"Игра запущена, но DLL так и не загрузилась. Подробности в launcher.log";
    return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Settings s = load();
    bool inject = true;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring a = argv[i];
        if ((a == L"--set" || a == L"--path") && i + 1 < argc) {
            s.exe_path = argv[++i];
            if (a == L"--set") { save(s); MessageBoxW(nullptr, s.exe_path.c_str(), L"Путь сохранён", MB_OK); return 0; }
        } else if (a == L"--dll" && i + 1 < argc) {
            s.dll_path = argv[++i];
            save(s);
        } else if (a == L"--args" && i + 1 < argc) {
            s.args = argv[++i];
            save(s);
        } else if (a == L"--no-inject") {
            inject = false;
        } else if (a == L"--help" || a == L"-h") {
            MessageBoxW(nullptr,
                L"launcher.exe [--set \"путь\\cs2.exe\"] [--path \"...\"]\n"
                L"[--dll \"путь\\skinchanger.dll\"] [--args \"-опции\"] [--no-inject]",
                kTitle, MB_OK);
            return 0;
        }
    }
    if (argv) LocalFree(argv);

    if (s.exe_path.empty()) {
        s.exe_path = pick_file(L"cs2.exe\0cs2.exe\0", L"Выбери cs2.exe (game\\bin\\win64)");
        if (s.exe_path.empty()) return 1;
        save(s);
    }
    if (!file_exists(s.exe_path)) {
        fail(L"Файл не найден:\n" + s.exe_path);
        return 1;
    }
    if (_wcsicmp(fs::path(s.exe_path).filename().c_str(), L"cs2.exe") != 0)
        log_line(L"внимание: файл называется не cs2.exe");

    fs::path dll = s.dll_path.empty() ? exe_dir() / kDllName : fs::path(s.dll_path);
    if (inject && !file_exists(dll)) {
        fail(L"Рядом с launcher.exe нет " + std::wstring(kDllName) +
             L"\nПоложи DLL в ту же папку или укажи --dll.");
        return 1;
    }
    if (!steam_running()) {
        if (MessageBoxW(nullptr, L"Steam не запущен — игра, скорее всего, сразу закроется.\nЗапустить Steam?",
                        kTitle, MB_ICONQUESTION | MB_YESNO) == IDYES) {
            ShellExecuteW(nullptr, L"open", L"steam://open/main", nullptr, nullptr, SW_SHOWNORMAL);
            Sleep(6000);
        }
    }

    log_line(L"---- старт, exe=" + s.exe_path + L", dll=" + dll.wstring());
    bool launched = false;
    std::wstring err;
    const bool injected = launch_and_inject(s.exe_path, s.args, inject ? dll : fs::path{}, launched, err);

    if (injected && inject) return 0;
    if (injected) return 0;
    fail(L"Игра запущена, но инъекция не удалась.\n\n" + err + L"\n\nЛог: launcher.log рядом с exe.");
    return 1;
}
