#pragma once

#include "config/config.h"

namespace quicktile {

class SingleInstanceGuard {
public:
    enum class AcquireResult {
        Acquired,
        AlreadyRunning,
        Failed,
    };

    SingleInstanceGuard() = default;
    ~SingleInstanceGuard();

    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;

    SingleInstanceGuard(SingleInstanceGuard&& other) noexcept;
    SingleInstanceGuard& operator=(SingleInstanceGuard&& other) noexcept;

    AcquireResult TryAcquire();
    DWORD lastError() const;

    static bool IsAlreadyRunningError(DWORD error);

private:
    void Reset();

    HANDLE handle_ = nullptr;
    DWORD lastError_ = ERROR_SUCCESS;
};

void ShowAlreadyRunningWarning();

}  // namespace quicktile