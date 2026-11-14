// Simple game launcher. Stores the path to cs2.exe in launcher.ini next to the
// exe and starts it with CreateProcess. No DLL injection here on purpose:
// keep "start the game" and "load a module into it" as two separate, auditable
// steps you run yourself.
//
// Usage:
//   launcher.exe                 -> uses the saved path, or asks for one
//   launcher.exe --set "C:\...\cs2.exe"   -> saves a new path without launching
//   launcher.exe --path "C:\...\cs2.exe"  -> launches this path once, without saving
//   launcher.exe --args "-insecure -novid" -> extra CS2 launch options (saved too)
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path ini_path() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path() / L"launcher.ini";
}

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string to_utf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

struct Settings {
    std::wstring exe_path;
    std::wstring args;
};

Settings load() {
    Settings s;
    std::ifstream in(ini_path());
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("exe_path=", 0) == 0) s.exe_path = to_wide(line.substr(9));
        else if (line.rfind("args=", 0) == 0) s.args = to_wide(line.substr(5));
    }
    return s;
}

void save(const Settings& s) {
    std::ofstream out(ini_path(), std::ios::trunc);
    out << "exe_path=" << to_utf8(s.exe_path) << "\n";
    out << "args=" << to_utf8(s.args) << "\n";
}

bool looks_like_cs2(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !fs::is_directory(p, ec) &&
          _wcsicmp(p.filename().c_str(), L"cs2.exe") == 0;
}

std::wstring prompt_for_path() {
    std::wcout << L"Путь к cs2.exe не задан.\n"
                  L"Обычно: ...\\Steam\\steamapps\\common\\Counter-Strike Global Offensive\\game\\bin\\win64\\cs2.exe\n"
                  L"Введи полный путь: ";
    std::wstring line;
    std::getline(std::wcin, line);
    // strip optional surrounding quotes from drag-and-drop paste
    if (line.size() >= 2 && line.front() == L'"' && line.back() == L'"') line = line.substr(1, line.size() - 2);
    return line;
}

bool launch(const std::wstring& exe, const std::wstring& args) {
    fs::path exe_path(exe);
    std::error_code ec;
    if (!fs::exists(exe_path, ec)) {
        std::wcerr << L"Файл не найден: " << exe << L"\n";
        return false;
    }
    if (!looks_like_cs2(exe_path))
        std::wcerr << L"Предупреждение: файл называется не cs2.exe (" << exe_path.filename().wstring()
                   << L"), запускаю как есть.\n";

    std::wstring cmdline = L"\"" + exe_path.wstring() + L"\"";
    if (!args.empty()) cmdline += L" " + args;

    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    // CreateProcess needs a writable buffer for the command line.
    std::wstring buf = cmdline;
    buf.push_back(L'\0');
    BOOL ok = CreateProcessW(exe_path.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                             exe_path.parent_path().c_str(), &si, &pi);
    if (!ok) {
        std::wcerr << L"CreateProcess не смог запустить игру, код ошибки " << GetLastError() << L"\n";
        return false;
    }
    std::wcout << L"Запущено: " << cmdline << L" (PID " << pi.dwProcessId << L")\n";
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Settings s = load();

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if ((a == L"--set" || a == L"--path") && i + 1 < argc) {
            s.exe_path = argv[++i];
            if (a == L"--set") { save(s); std::wcout << L"Сохранено: " << s.exe_path << L"\n"; return 0; }
        } else if (a == L"--args" && i + 1 < argc) {
            s.args = argv[++i];
            save(s);
        } else if (a == L"--help" || a == L"-h") {
            std::wcout << L"launcher.exe [--set \"путь\\cs2.exe\"] [--path \"путь\\cs2.exe\"] [--args \"-опции\"]\n";
            return 0;
        }
    }

    if (s.exe_path.empty()) {
        s.exe_path = prompt_for_path();
        if (s.exe_path.empty()) { std::wcerr << L"Путь не введён.\n"; return 1; }
        save(s);
    }

    return launch(s.exe_path, s.args) ? 0 : 1;
}
