#pragma once

#include "config/config.h"

#include <string_view>

namespace quicktile {

class Logger;

class Event {
public:
    Event() = default;
    Event(BOOL manualReset, BOOL initialState);
    ~Event();

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&& other) noexcept;
    Event& operator=(Event&& other) noexcept;

    HANDLE Get() const;
    bool IsValid() const;
    void Set() const;
    void Reset();

private:
    HANDLE handle_ = nullptr;
};

class FileWatcher {
public:
    FileWatcher(std::wstring_view filePath, HWND notifyWindow, UINT changedMessage, Logger& logger);
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    void Stop();

private:
    struct WatcherThreadContext {
        Logger* logger = nullptr;
        HANDLE directoryHandle = nullptr;
        Event* stopEvent = nullptr;
        HWND window = nullptr;
        UINT changedMessage = 0;
        std::wstring fileName;
    };

    static bool IsTargetFileChange(const FILE_NOTIFY_INFORMATION* info, const std::wstring& targetFileName);
    static DWORD WINAPI WatcherThreadProc(LPVOID contextPointer);
    void Start(std::wstring_view filePath, HWND notifyWindow, UINT changedMessage, Logger& logger);

    HANDLE directoryHandle_ = nullptr;
    HANDLE watcherThread_ = nullptr;
    Event stopEvent_;
};

}  // namespace quicktile