#include "platform/logger.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>

namespace quicktile {

std::filesystem::path Logger::ResolveLogPath() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return std::filesystem::path(L"quicktile.log");
    }

    return std::filesystem::path(buffer) / L"QuickTile" / L"quicktile.log";
}

std::wstring Logger::Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::wostringstream stream;
    stream << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S");
    return stream.str();
}

void Logger::WriteLine(std::wstring_view level, std::wstring_view message) {
    std::lock_guard<std::mutex> lock(logMutex_);
    std::wostringstream line;
    line << L'[' << Timestamp() << L"] [" << level << L"] " << message << L"\r\n";
    const std::wstring text = line.str();

    OutputDebugStringW(text.c_str());

    std::ofstream file(logPath_, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        return;
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return;
    }

    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
}

std::wstring Logger::FormatWin32Message(unsigned long errorCode) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, errorCode, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (length == 0 || buffer == nullptr) {
        std::wostringstream stream;
        stream << L"Win32 error " << errorCode;
        return stream.str();
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' || message.back() == L'\t')) {
        message.pop_back();
    }

    return message;
}

Logger::Logger()
    : logPath_(ResolveLogPath()) {
    std::error_code error;
    if (logPath_.has_parent_path()) {
        std::filesystem::create_directories(logPath_.parent_path(), error);
    }
    Info(L"QuickTile logging initialized");
}

Logger::~Logger() {
    Info(L"QuickTile logging shutdown");
}

void Logger::Info(std::wstring_view message) {
    WriteLine(L"INFO", message);
}

void Logger::Error(std::wstring_view message) {
    WriteLine(L"ERROR", message);
}

void Logger::ErrorLastWin32(std::wstring_view context, unsigned long errorCode) {
    std::wostringstream stream;
    stream << context << L": " << FormatWin32Message(errorCode) << L" (" << errorCode << L')';
    Error(stream.str());
}

bool Logger::Clear() {
    std::lock_guard<std::mutex> lock(logMutex_);

    std::error_code error;
    if (logPath_.has_parent_path()) {
        std::filesystem::create_directories(logPath_.parent_path(), error);
    }

    std::ofstream file(logPath_, std::ios::binary | std::ios::trunc);
    return file.is_open();
}

std::wstring Logger::FilePath() const {
    return logPath_.wstring();
}

}  // namespace quicktile