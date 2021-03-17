// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"sync"

	"syscall/zx"
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

	// ignoreDuplicates indicates whether a new entry should be ignored if its
	// content is the same as the last entry in the circular logs.
	ignoreDuplicates bool
}

// MakeCircularLogs initializes a CircularLogs struct with the given capacity.
//
// This function will panic if it is called with a zero capacity.
func MakeCircularLogs(capacity int, ignoreDuplicates bool) CircularLogs {
	if capacity == 0 {
		panic("attempted to create CircularLogs with a zero capacity")
	}
	c := CircularLogs{
		capacity:         capacity,
		ignoreDuplicates: ignoreDuplicates,
	}
	c.mu.logs = make([]LogEntry, 0, capacity)
	return c
}

// Push records a new entry in the logs.
//
// If the capacity of the logs has been reached, the new entry will replace the
// oldest entry. If ignoreDuplicates is true and the new entry has the same
// content as the last entry added, nothing will happen.
func (c *CircularLogs) Push(newEntry LogEntry) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.ignoreDuplicates && c.lastEntryHasSameContent(newEntry.Content) {
		return
	}

	if len(c.mu.logs) < c.capacity {
		c.mu.logs = append(c.mu.logs, newEntry)
	} else {
		c.mu.logs[c.first] = newEntry
		c.first = (c.first + 1) % c.capacity
	}
}

// lastEntryHasSameContent returns true if the last entry has the same content
// than the given content.
//
// Precondition: c.mu must be locked.
func (c *CircularLogs) lastEntryHasSameContent(content string) bool {
	if len(c.mu.logs) == 0 {
		return false
	}
	lastEntryIndex := (c.first + len(c.mu.logs) - 1) % c.capacity
	return c.mu.logs[lastEntryIndex].Content == content
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
