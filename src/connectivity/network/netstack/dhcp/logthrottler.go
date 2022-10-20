// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package dhcp

import (
	"fmt"
	"time"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"
	"gvisor.dev/gvisor/pkg/tcpip"
)

type logThrottler struct {
	clock tcpip.Clock
	mu    struct {
		sync.Mutex
		state map[string]logThrottlerMapState
	}
}

type logThrottlerMapState struct {
	lastAttemptedToLog tcpip.MonotonicTime
	lastActuallyLogged tcpip.MonotonicTime
	timesSuppressed    int
}

const (
	throttleIfAttemptedToLogWithin = 70 * time.Second
	allowLoggingIfSuppressedFor    = time.Hour
	maxLogThrottlerStateSize       = 10
)

func (t *logThrottler) init(clock tcpip.Clock) {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.clock = clock
	t.mu.state = make(map[string]logThrottlerMapState)
}

func (t *logThrottler) enabled() bool {
	return t.clock != nil
}

// shouldLog returns whether the log line with the given tag should be logged,
// and how many times it's been suppressed before this point since it was last
// logged
func (t *logThrottler) shouldLog(logLine string) (bool, int) {
	t.mu.Lock()
	defer t.mu.Unlock()

	state, ok := t.mu.state[logLine]
	now := t.clock.NowMonotonic()
	if !ok {
		t.mu.state[logLine] = logThrottlerMapState{
			lastAttemptedToLog: now,
			lastActuallyLogged: now,
		}
		t.enforceMaxSizeLocked()
		return true, 0
	}

	lastAttempt := state.lastAttemptedToLog
	state.lastAttemptedToLog = now

	timesSuppressed := state.timesSuppressed
	willLog := (lastAttempt.Add(throttleIfAttemptedToLogWithin).Before(now) ||
		state.lastActuallyLogged.Add(allowLoggingIfSuppressedFor).Before(now))
	if willLog {
		state.lastActuallyLogged = now
		state.timesSuppressed = 0
	} else {
		state.timesSuppressed++
	}

	t.mu.state[logLine] = state
	return willLog, timesSuppressed
}

func (t *logThrottler) logTf(level syslog.LogLevel, tag, format string, a ...interface{}) error {
	if !t.enabled() {
		return syslog.Logf(syslog.DefaultCallDepth, level, tag, format, a...)
	}

	line := fmt.Sprintf(format, a...)
	if ok, n := t.shouldLog(line); ok {
		if n == 0 {
			return syslog.Logf(syslog.DefaultCallDepth, level, tag, line)
		} else {
			return syslog.Logf(syslog.DefaultCallDepth, level, tag, "%s (log throttled %d times)", line, n)
		}
	}
	return nil
}

func (t *logThrottler) enforceMaxSizeLocked() {
	if len(t.mu.state) <= maxLogThrottlerStateSize {
		return
	}
	// evict least-recently used
	oldest := t.clock.NowMonotonic()
	var oldestTag string
	for tag, state := range t.mu.state {
		if state.lastAttemptedToLog.Before(oldest) {
			oldest = state.lastAttemptedToLog
			oldestTag = tag
		}
	}
	delete(t.mu.state, oldestTag)
}

func (t *logThrottler) reset() {
	if !t.enabled() {
		return
	}

	t.mu.Lock()
	defer t.mu.Unlock()
	t.mu.state = make(map[string]logThrottlerMapState)
}
