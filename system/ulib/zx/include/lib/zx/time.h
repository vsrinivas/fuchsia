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

    constexpr duration operator+(duration other) const {
        return duration(value_ + other.value_);
    }

    constexpr duration operator-(duration other) const {
        return duration(value_ - other.value_);
    }

    constexpr duration operator*(uint64_t multiplier) const {
        return duration(value_ * multiplier);
    }

    constexpr duration operator/(uint64_t divisor) const {
        return duration(value_ / divisor);
    }

    constexpr duration operator%(duration divisor) const {
        return duration(value_ % divisor.value_);
    }

    constexpr uint64_t operator/(duration other) const {
        return value_ / other.value_;
    }

    duration& operator+=(duration other) {
        value_ += other.value_;
        return *this;
    }

    duration& operator-=(duration other) {
      value_ -= other.value_;
      return *this;
    }

    duration& operator*=(uint64_t multiplier) {
        value_ *= multiplier;
        return *this;
    }

    duration& operator/=(uint64_t divisor) {
      value_ /= divisor;
      return *this;
    }

    constexpr bool operator==(duration other) const { return value_ == other.value_; }
    constexpr bool operator!=(duration other) const { return value_ != other.value_; }
    constexpr bool operator<(duration other) const { return value_ < other.value_; }
    constexpr bool operator<=(duration other) const { return value_ <= other.value_; }
    constexpr bool operator>(duration other) const { return value_ > other.value_; }
    constexpr bool operator>=(duration other) const { return value_ >= other.value_; }

    constexpr uint64_t to_nsecs() const { return value_; }

    constexpr uint64_t to_usecs() const { return value_ / ZX_USEC(1); }

    constexpr uint64_t to_msecs() const { return value_ / ZX_MSEC(1); }

    constexpr uint64_t to_secs() const { return value_ / ZX_SEC(1); }

    constexpr uint64_t to_mins() const { return value_ / ZX_MIN(1); }

    constexpr uint64_t to_hours() const { return value_ / ZX_HOUR(1); }

private:
    zx_duration_t value_ = 0;
};

class ticks {
public:
    constexpr ticks() = default;

    explicit constexpr ticks(zx_ticks_t value) : value_(value) {}

    // Constructs a tick object for the current tick counter in the system.
    static ticks now() { return ticks(zx_ticks_get()); }

    // Returns the number of ticks contained within one second.
    static ticks per_second() { return ticks(zx_ticks_per_second()); }

    // Acquires the number of ticks contained within this object.
    constexpr zx_ticks_t get() const { return value_; }

    constexpr ticks operator+(ticks other) const {
        return ticks(value_ + other.value_);
    }

    constexpr ticks operator-(ticks other) const {
        return ticks(value_ - other.value_);
    }

    constexpr ticks operator*(uint64_t multiplier) const {
        return ticks(value_ * multiplier);
    }

    constexpr ticks operator/(uint64_t divisor) const {
        return ticks(value_ / divisor);
    }

    constexpr uint64_t operator/(ticks other) const {
        return value_ / other.value_;
    }

    ticks& operator+=(ticks other) {
        value_ += other.value_;
        return *this;
    }

    ticks& operator-=(ticks other) {
      value_ -= other.value_;
      return *this;
    }

    ticks& operator*=(uint64_t multiplier) {
        value_ *= multiplier;
        return *this;
    }

    ticks& operator/=(uint64_t divisor) {
      value_ /= divisor;
      return *this;
    }

    constexpr bool operator==(ticks other) const { return value_ == other.value_; }
    constexpr bool operator!=(ticks other) const { return value_ != other.value_; }
    constexpr bool operator<(ticks other) const { return value_ < other.value_; }
    constexpr bool operator<=(ticks other) const { return value_ <= other.value_; }
    constexpr bool operator>(ticks other) const { return value_ > other.value_; }
    constexpr bool operator>=(ticks other) const { return value_ >= other.value_; }

private:
    zx_ticks_t value_ = 0;
};

template <uint32_t kClockId>
class base_time {
public:
    constexpr base_time() = default;

    explicit constexpr base_time(zx_time_t value) : value_(value) {}

    static constexpr base_time<kClockId> infinite() { return base_time<kClockId>(ZX_TIME_INFINITE); }

    constexpr zx_time_t get() const { return value_; }

    zx_time_t* get_address() { return &value_; }

    constexpr duration operator-(base_time<kClockId> other) const {
        return duration(value_ - other.value_);
    }

    constexpr base_time<kClockId> operator+(duration delta) const {
        return base_time<kClockId>(value_ + delta.get());
    }

    constexpr base_time<kClockId> operator-(duration delta) const {
        return base_time<kClockId>(value_ - delta.get());
    }

    base_time<kClockId>& operator+=(duration delta) {
      value_ += delta.get();
      return *this;
    }

    base_time<kClockId>& operator-=(duration delta) {
      value_ -= delta.get();
      return *this;
    }

    constexpr bool operator==(base_time<kClockId> other) const { return value_ == other.value_; }
    constexpr bool operator!=(base_time<kClockId> other) const { return value_ != other.value_; }
    constexpr bool operator<(base_time<kClockId> other) const { return value_ < other.value_; }
    constexpr bool operator<=(base_time<kClockId> other) const { return value_ <= other.value_; }
    constexpr bool operator>(base_time<kClockId> other) const { return value_ > other.value_; }
    constexpr bool operator>=(base_time<kClockId> other) const { return value_ >= other.value_; }

private:
    zx_time_t value_ = 0;
};

using time = base_time<ZX_CLOCK_MONOTONIC>;
using time_utc = base_time<ZX_CLOCK_UTC>;
using time_thread = base_time<ZX_CLOCK_THREAD>;

namespace clock {

template <uint32_t kClockId>
static inline base_time<kClockId> get() {
    return base_time<kClockId>(zx_clock_get(kClockId));
}

} // namespace clock

constexpr inline duration nsec(uint64_t n) { return duration(ZX_NSEC(n)); }

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
