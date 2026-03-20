#include "platform/single_instance.h"

namespace {

constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\QuickTile.SingleInstance";
constexpr wchar_t kQuickTileTitle[] = L"QuickTile";
constexpr wchar_t kAlreadyRunningMessage[] =
    L"QuickTile is already running. Close the existing instance before starting another one.";

}  // namespace

namespace quicktile {

SingleInstanceGuard::~SingleInstanceGuard() {
    Reset();
}

SingleInstanceGuard::SingleInstanceGuard(SingleInstanceGuard&& other) noexcept
    : handle_(other.handle_), lastError_(other.lastError_) {
    other.handle_ = nullptr;
    other.lastError_ = ERROR_SUCCESS;
}

SingleInstanceGuard& SingleInstanceGuard::operator=(SingleInstanceGuard&& other) noexcept {
    if (this != &other) {
        Reset();
        handle_ = other.handle_;
        lastError_ = other.lastError_;
        other.handle_ = nullptr;
        other.lastError_ = ERROR_SUCCESS;
    }

    return *this;
}

SingleInstanceGuard::AcquireResult SingleInstanceGuard::TryAcquire() {
    if (handle_ != nullptr) {
        return AcquireResult::Acquired;
    }

    SetLastError(ERROR_SUCCESS);
    HANDLE handle = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    lastError_ = GetLastError();

    if (handle == nullptr) {
        return IsAlreadyRunningError(lastError_) ? AcquireResult::AlreadyRunning : AcquireResult::Failed;
    }

    if (IsAlreadyRunningError(lastError_)) {
        CloseHandle(handle);
        return AcquireResult::AlreadyRunning;
    }

    handle_ = handle;
    return AcquireResult::Acquired;
}

DWORD SingleInstanceGuard::lastError() const {
    return lastError_;
}

bool SingleInstanceGuard::IsAlreadyRunningError(DWORD error) {
    return error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED;
}

void SingleInstanceGuard::Reset() {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

void ShowAlreadyRunningWarning() {
    MessageBoxW(nullptr, kAlreadyRunningMessage, kQuickTileTitle, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

}  // namespace quicktile