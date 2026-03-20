#include "platform/file_watcher.h"

#include "platform/logger.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace quicktile {

bool FileWatcher::IsTargetFileChange(const FILE_NOTIFY_INFORMATION* info, const std::wstring& targetFileName) {
    const std::wstring changedName(info->FileName, info->FileNameLength / sizeof(WCHAR));
    return _wcsicmp(changedName.c_str(), targetFileName.c_str()) == 0;
}

DWORD WINAPI FileWatcher::WatcherThreadProc(LPVOID contextPointer) {
    std::unique_ptr<WatcherThreadContext> context(reinterpret_cast<WatcherThreadContext*>(contextPointer));
    if (context == nullptr || context->directoryHandle == nullptr || context->stopEvent == nullptr || !context->stopEvent->IsValid()) {
        if (context != nullptr && context->logger != nullptr) {
            context->logger->Error(L"File watcher thread started without valid context");
        }
        return 0;
    }

    std::array<std::byte, 4096> buffer{};

    while (true) {
        OVERLAPPED overlapped{};
        quicktile::Event overlappedEvent(TRUE, FALSE);
        overlapped.hEvent = overlappedEvent.Get();

        DWORD bytesReturned = 0;
        const BOOL readStarted = ReadDirectoryChangesW(
            context->directoryHandle,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
            nullptr,
            &overlapped,
            nullptr);

        if (!readStarted) {
            context->logger->ErrorLastWin32(L"ReadDirectoryChangesW failed for file watcher", GetLastError());
            return 0;
        }

        HANDLE waitHandles[] = {context->stopEvent->Get(), overlapped.hEvent};
        const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0) {
            CancelIoEx(context->directoryHandle, &overlapped);
            WaitForSingleObject(overlapped.hEvent, INFINITE);
            return 0;
        }

        if (waitResult != WAIT_OBJECT_0 + 1 ||
            !GetOverlappedResult(context->directoryHandle, &overlapped, &bytesReturned, FALSE)) {
            context->logger->ErrorLastWin32(L"File watcher failed waiting for directory changes", GetLastError());
            return 0;
        }

        const auto* notification = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data());
        bool targetChanged = false;
        while (notification != nullptr) {
            if (IsTargetFileChange(notification, context->fileName)) {
                targetChanged = true;
                break;
            }

            if (notification->NextEntryOffset == 0) {
                notification = nullptr;
            } else {
                notification = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<const std::byte*>(notification) + notification->NextEntryOffset);
            }
        }

        if (targetChanged && context->window != nullptr && context->changedMessage != 0) {
            if (!PostMessageW(context->window, context->changedMessage, 0, 0)) {
                context->logger->ErrorLastWin32(L"Failed to notify UI thread about file change", GetLastError());
            }
        }
    }

    return 0;
}

Event::Event(BOOL manualReset, BOOL initialState)
    : handle_(CreateEventW(nullptr, manualReset, initialState, nullptr)) {
}

Event::~Event() {
    Reset();
}

Event::Event(Event&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = nullptr;
}

Event& Event::operator=(Event&& other) noexcept {
    if (this != &other) {
        Reset();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }

    return *this;
}

HANDLE Event::Get() const {
    return handle_;
}

bool Event::IsValid() const {
    return handle_ != nullptr;
}

void Event::Set() const {
    SetEvent(handle_);
}

void Event::Reset() {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

void FileWatcher::Start(std::wstring_view filePath, HWND notifyWindow, UINT changedMessage, Logger& logger) {
    const std::filesystem::path watchedPath(filePath);
    const std::filesystem::path watchedDirectory = watchedPath.parent_path();
    if (watchedDirectory.empty()) {
        return;
    }

    directoryHandle_ = CreateFileW(
        watchedDirectory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (directoryHandle_ == INVALID_HANDLE_VALUE) {
        logger.ErrorLastWin32(L"Failed to open watched directory", GetLastError());
        directoryHandle_ = nullptr;
        return;
    }

    stopEvent_ = Event(TRUE, FALSE);

    std::unique_ptr<WatcherThreadContext> context;
    try {
        context = std::make_unique<WatcherThreadContext>(WatcherThreadContext{
            &logger,
            directoryHandle_,
            &stopEvent_,
            notifyWindow,
            changedMessage,
            watchedPath.filename().wstring(),
        });
    } catch (const std::bad_alloc&) {
        logger.Error(L"Failed to allocate file watcher context");
        stopEvent_.Reset();
        CloseHandle(directoryHandle_);
        directoryHandle_ = nullptr;
        return;
    }

    WatcherThreadContext* const threadContext = context.release();
    watcherThread_ = CreateThread(nullptr, 0, WatcherThreadProc, threadContext, 0, nullptr);
    if (watcherThread_ == nullptr) {
        std::unique_ptr<WatcherThreadContext> threadContextGuard(threadContext);
        logger.ErrorLastWin32(L"Failed to create file watcher thread", GetLastError());
        stopEvent_.Reset();
        CloseHandle(directoryHandle_);
        directoryHandle_ = nullptr;
        return;
    }
}

FileWatcher::FileWatcher(std::wstring_view filePath, HWND notifyWindow, UINT changedMessage, Logger& logger) {
    Start(filePath, notifyWindow, changedMessage, logger);
}

void FileWatcher::Stop() {
    if (!stopEvent_.IsValid()) {
        return;
    }

    stopEvent_.Set();
    if (watcherThread_ != nullptr) {
        WaitForSingleObject(watcherThread_, INFINITE);
        CloseHandle(watcherThread_);
        watcherThread_ = nullptr;
    }

    stopEvent_.Reset();

    if (directoryHandle_ != nullptr) {
        CloseHandle(directoryHandle_);
        directoryHandle_ = nullptr;
    }
}

FileWatcher::~FileWatcher() {
    Stop();
}

}  // namespace quicktile