#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace quicktile {

class Logger {
public:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void Info(std::wstring_view message);
    void Error(std::wstring_view message);
    void ErrorLastWin32(std::wstring_view context, unsigned long errorCode);
    bool Clear();
    std::wstring FilePath() const;

private:
    static std::filesystem::path ResolveLogPath();
    static std::wstring Timestamp();
    static std::wstring FormatWin32Message(unsigned long errorCode);
    void WriteLine(std::wstring_view level, std::wstring_view message);

    std::filesystem::path logPath_;
    std::mutex logMutex_;
};

}  // namespace quicktile