// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/syscalls.h>

namespace zx {

class duration {
public:
    constexpr duration() = default;

    explicit constexpr duration(zx_duration_t value) : value_(value) {}

    static constexpr duration infinite() { return duration(ZX_TIME_INFINITE); }

    constexpr zx_duration_t value() const { return value_; }

    constexpr duration operator-(duration other) const {
        return duration(value_ - other.value_);
    }

    constexpr duration operator+(duration other) const {
        return duration(value_ + other.value_);
    }

    constexpr duration operator/(int64_t divisor) const {
        return duration(value_ / divisor);
    }

    constexpr uint64_t operator/(duration other) const {
        return value_ / other.value_;
    }

    constexpr duration operator*(int64_t multiplier) const {
        return duration(value_ * multiplier);
    }

    constexpr bool operator==(duration other) const { return value_ == other.value_; }
    constexpr bool operator!=(duration other) const { return value_ != other.value_; }
    constexpr bool operator<(duration other) const { return value_ < other.value_; }
    constexpr bool operator<=(duration other) const { return value_ <= other.value_; }
    constexpr bool operator>(duration other) const { return value_ > other.value_; }
    constexpr bool operator>=(duration other) const { return value_ >= other.value_; }

private:
    zx_duration_t value_ = 0;
};

class time {
public:
    constexpr time() = default;

    explicit constexpr time(zx_time_t value) : value_(value) {}

    static constexpr time infinite() { return  time(ZX_TIME_INFINITE); }

    // TODO(abarth): Return zx::time rather than zx_time_t.
    static inline zx_time_t get(uint32_t clock_id) {
        return zx_time_get(clock_id);
    }

    constexpr zx_time_t value() const { return value_; }

    constexpr duration operator-(time other) const {
        return duration(value_ - other.value_);
    }

    constexpr time operator+(duration delta) const {
        return time(value_ + delta.value());
    }

    constexpr time operator-(duration delta) const {
        return time(value_ - delta.value());
    }

    constexpr bool operator==(time other) const { return value_ == other.value_; }
    constexpr bool operator!=(time other) const { return value_ != other.value_; }
    constexpr bool operator<(time other) const { return value_ < other.value_; }
    constexpr bool operator<=(time other) const { return value_ <= other.value_; }
    constexpr bool operator>(time other) const { return value_ > other.value_; }
    constexpr bool operator>=(time other) const { return value_ >= other.value_; }

private:
    zx_time_t value_ = 0;
};

constexpr inline duration usec(uint64_t n) { return duration(ZX_USEC(n)); }

constexpr inline duration msec(uint64_t n) { return duration(ZX_MSEC(n)); }

constexpr inline duration sec(uint64_t n) { return duration(ZX_SEC(n)); }

constexpr inline duration min(uint64_t n) { return duration(ZX_MIN(n)); }

constexpr inline duration hour(uint64_t n) { return duration(ZX_HOUR(n)); }

// TODO(abarth): Remove.
inline zx_status_t nanosleep(zx_time_t deadline) {
    return zx_nanosleep(deadline);
}

inline zx_status_t nanosleep(zx::time deadline) {
    return zx_nanosleep(deadline.value());
}

// TODO(abarth): Remove.
inline zx_time_t deadline_after(zx_duration_t nanoseconds) {
    return zx_deadline_after(nanoseconds);
}

inline time deadline_after(zx::duration nanoseconds) {
    return time(zx_deadline_after(nanoseconds.value()));
}

} // namespace zx
