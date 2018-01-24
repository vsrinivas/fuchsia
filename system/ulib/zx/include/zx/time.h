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

    constexpr zx_duration_t get() const { return value_; }

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

    constexpr zx_time_t get() const { return value_; }

    constexpr duration operator-(time other) const {
        return duration(value_ - other.value_);
    }

    constexpr time operator+(duration delta) const {
        return time(value_ + delta.get());
    }

    constexpr time operator-(duration delta) const {
        return time(value_ - delta.get());
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

namespace clock {

static inline time get(uint32_t clock_id) {
    return time(zx_clock_get(clock_id));
}

} // namespace clock

constexpr inline duration usec(uint64_t n) { return duration(ZX_USEC(n)); }

constexpr inline duration msec(uint64_t n) { return duration(ZX_MSEC(n)); }

constexpr inline duration sec(uint64_t n) { return duration(ZX_SEC(n)); }

constexpr inline duration min(uint64_t n) { return duration(ZX_MIN(n)); }

constexpr inline duration hour(uint64_t n) { return duration(ZX_HOUR(n)); }

inline zx_status_t nanosleep(zx::time deadline) {
    return zx_nanosleep(deadline.get());
}

inline time deadline_after(zx::duration nanoseconds) {
    return time(zx_deadline_after(nanoseconds.get()));
}

} // namespace zx
