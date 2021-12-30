// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"syscall/zx"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/sync"
)

type LogEntry struct {
	Timestamp zx.Time
	Content   string
}

// CircularLogs is a circular array of logs.
//
// It must be created using the MakeCircularLogs function.
type CircularLogs struct {
	mu struct {
		sync.Mutex
		logs []LogEntry
	}

	// capacity is the maximum number of entries we store before we start erasing
	// oldest entries.
	capacity int

	// first points to the oldest entry in the logs.
	first int
}

// MakeCircularLogs initializes a CircularLogs struct with the given capacity.
//
// This function will panic if it is called with a zero capacity.
func MakeCircularLogs(capacity int) CircularLogs {
	if capacity == 0 {
		panic("attempted to create CircularLogs with a zero capacity")
	}
	c := CircularLogs{
		capacity: capacity,
	}
	c.mu.logs = make([]LogEntry, 0, capacity)
	return c
}

// Push records a new entry in the logs.
//
// The timestamp of the entry is set to the current monotonic time.
//
// If the capacity of the logs has been reached, the new entry will replace the
// oldest entry.
func (c *CircularLogs) Push(content string) {
	c.mu.Lock()
	defer c.mu.Unlock()

	newEntry := LogEntry{
		Timestamp: zx.Sys_clock_get_monotonic(),
		Content:   content,
	}

	if len(c.mu.logs) < c.capacity {
		c.mu.logs = append(c.mu.logs, newEntry)
	} else {
		c.mu.logs[c.first] = newEntry
		c.first = (c.first + 1) % c.capacity
	}
}

// BuildLogs copies the content of the circular array in a new slice.
//
// The log entries are ordered from oldest to newest.
func (c *CircularLogs) BuildLogs() []LogEntry {
	c.mu.Lock()
	defer c.mu.Unlock()

	all := make([]LogEntry, 0, len(c.mu.logs))
	all = append(all, c.mu.logs[c.first:]...)
	all = append(all, c.mu.logs[:c.first]...)

	return all
}
