// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package time defines newtypes around zx.Time and zx.Duration that
// provide a subset of the std time.Time and time.Duration API.
//
// The std time package unfortunately [blurs the
// distinction](https://pkg.go.dev/time#hdr-Monotonic_Clocks) between wallclock
// and monotonic times. Operations from std time provide no type safety to
// preserve monotonicity in time values. Indeed, some std time methods and
// functions will silently erase the monotonic component of the value.
// This package defines its newtypes as monotonic time values and as such are
// intended for use in setting deadlines and expirations in Fuchsia netstack.
//
// Users of this package should use it in place of the std time package. Given
// the blurred semantics of std time, using this package alongside std time is
// potentially hazardous and should only be done with care.
package time

import (
	"context"
	"fmt"
	"math"
	"syscall/zx"
	"time"
)

const (
	Nanosecond  Duration = 1
	Microsecond          = 1000 * Nanosecond
	Millisecond          = 1000 * Microsecond
	Second               = 1000 * Millisecond
	Minute               = 60 * Second
	Hour                 = 60 * Minute
)

// A Time represents a monotonic instant in time since system start with
// nanosecond precision.
type Time struct {
	value zx.Time
}

// Now returns the current monotonic time relative to system start.
func Now() Time {
	return Time{zx.Sys_clock_get_monotonic()}
}

// Add returns the Time t+d. If the sum is such as to exceed the maximum or
// minimum Time value, the maximum or minimum will be returned, respectively.
func (t Time) Add(d Duration) Time {
	if d > 0 && int64(t.value) > math.MaxInt64-int64(d) {
		return Time{math.MaxInt64}
	}
	if d < 0 && int64(t.value) < math.MinInt64-int64(d) {
		return Time{math.MinInt64}
	}
	return Time{t.value + zx.Time(d)}
}

// Sub returns the Duration t-u. If the difference is such as to exceed the
// maximum or minimum Duration value, the maximum or minimum will be returned,
// respectively.
func (t Time) Sub(u Time) Duration {
	if u.value > 0 && int64(t.value) > math.MinInt64-int64(u.value) {
		return Duration(math.MinInt64)
	}
	if u.value < 0 && int64(t.value) > math.MaxInt64+int64(u.value) {
		return Duration(math.MaxInt64)
	}
	return Duration(int64(t.value) - int64(u.value))
}

// Before returns whether the instant t is before u.
func (t Time) Before(u Time) bool {
	return t.value < u.value
}

// String returns the time formatted using the format string:
//     "m=+-<value>"
// where m indicates the monotonic component of the value. This format is the
// analogue of the std time.Time.String() format, with the non-monotonic
// components of the string removed. String presents its value in decimal units
// of seconds. String is intended solely for debugging.
func (t Time) String() string {
	d, prefix := func(n int64) (uint64, rune) {
		if n < 0 {
			return uint64(-n), '-'
		}
		return uint64(n), '+'
	}(t.MonotonicNano())
	seconds := d / 1e9
	millis := d % 1e9
	return fmt.Sprintf("m=%c%d.%09d", prefix, seconds, millis)
}

// MonotonicNano returns the time as a nanosecond count. It is the analogue of
// std time.Time.UnixNano() but without any reference to the Unix epoch.
func (t Time) MonotonicNano() int64 {
	return int64(t.value)
}

// Monotonic returns the monotonic time corresponding to the given nanosecond
// count. It is the analogue of std time.Unix() but without any reference to the
// Unix epoch.
func Monotonic(n int64) Time {
	return Time{zx.Time(n)}
}

// After waits for duration to elapse and then sends the current monotonic time
// on the returned channel.
func After(d Duration) <-chan Time {
	ch := make(chan Time, 1)
	_ = time.AfterFunc(d.stdDuration(), func() {
		ch <- Now()
	})
	return ch
}

// A Duration represents the nanosecond count between two Time instants.
type Duration zx.Duration

// Nanoseconds returns the duration as an integer nanosecond count.
func (d Duration) Nanoseconds() int64 {
	return d.stdDuration().Nanoseconds()
}

// Microseconds returns the duration as an integer microsecond count.
func (d Duration) Microseconds() int64 {
	return d.stdDuration().Microseconds()
}

// Milliseconds returns the duration as an integer millisecond count.
func (d Duration) Milliseconds() int64 {
	return d.stdDuration().Milliseconds()
}

// Seconds returns the duration as floating point number of seconds.
func (d Duration) Seconds() float64 {
	return d.stdDuration().Seconds()
}

func (d Duration) stdDuration() time.Duration {
	return time.Duration(d)
}

// String returns the string representation of the duration in the default form
// used by time.Duration.
func (d Duration) String() string {
	return d.stdDuration().String()
}

// Sleep pauses the current goroutine for at least the duration. A 0 or negative
// duration returns immediately.
func Sleep(d Duration) {
	time.Sleep(d.stdDuration())
}

// Since returns the time elapsed since t.
func Since(t Time) Duration {
	return Now().Sub(t)
}

// ContextWithTimeout returns a copy of the parent context with its timeout set
// to the value of the supplied duration. This function allows current users of
// Duration to avoid a direct dependency on the std time package which has
// different semantics and should not be freely used with the types in this
// package.
func ContextWithTimeout(parent context.Context, d Duration) (context.Context, context.CancelFunc) {
	return context.WithTimeout(parent, d.stdDuration())
}
