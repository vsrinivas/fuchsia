// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package dhcp

import (
	"testing"
	"time"

	"gvisor.dev/gvisor/pkg/tcpip/faketime"
)

func check(t *testing.T, logLine string, gotOk, wantOk bool, gotN, wantN int) {
	t.Helper()
	if gotOk != wantOk || gotN != wantN {
		t.Errorf("gotShouldLog(_, %q) = (%t, %d), want (%t, %d)", logLine, gotOk, gotN, wantOk, wantN)
	}
}

func TestLogThrottler(t *testing.T) {
	clock := faketime.NewManualClock()

	var throttler logThrottler
	throttler.init(clock)

	aLogLine := "a log line"

	t.Run("should not throttle new log line", func(t *testing.T) {
		ok, n := throttler.shouldLog(aLogLine)
		check(t, aLogLine, ok, true, n, 0)
	})

	t.Run("should throttle log line if we logged it too recently", func(t *testing.T) {
		clock.Advance(throttleIfAttemptedToLogWithin - time.Nanosecond)
		ok, n := throttler.shouldLog(aLogLine)
		check(t, aLogLine, ok, false, n, 0)
	})

	t.Run("should throttle log line if we _tried_ to log it too recently", func(t *testing.T) {
		clock.Advance(throttleIfAttemptedToLogWithin - time.Nanosecond)
		ok, n := throttler.shouldLog(aLogLine)
		check(t, aLogLine, ok, false, n, 1)
	})

	t.Run("should not throttle different log line", func(t *testing.T) {
		aDifferentLogLine := "a different log line"
		ok, n := throttler.shouldLog(aDifferentLogLine)
		check(t, aDifferentLogLine, ok, true, n, 0)
	})

	t.Run("should log if we haven't tried to log it too recently", func(t *testing.T) {
		clock.Advance(throttleIfAttemptedToLogWithin + time.Nanosecond)
		ok, n := throttler.shouldLog(aLogLine)
		check(t, aLogLine, ok, true, n, 2)
	})

	t.Run("should log if the state has been reset", func(t *testing.T) {
		throttler.reset()
		ok, n := throttler.shouldLog(aLogLine)
		check(t, aLogLine, ok, true, n, 0)
	})

	t.Run("should log if we've been throttling for too long", func(t *testing.T) {
		totalDuration := 0 * time.Second
		numIterations := 0
		increment := throttleIfAttemptedToLogWithin - time.Nanosecond

		for totalDuration < allowLoggingIfSuppressedFor {
			ok, n := throttler.shouldLog(aLogLine)
			check(t, aLogLine, ok, false, n, numIterations)

			clock.Advance(increment)
			totalDuration += increment
			numIterations++
		}
		ok, n := throttler.shouldLog(aLogLine)
		check(t, aLogLine, ok, true, n, numIterations)
	})
}
