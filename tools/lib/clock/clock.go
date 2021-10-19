// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package clock

import (
	"context"
	"sync"
	"time"
)

type clock interface {
	Now() time.Time
	After(d time.Duration) <-chan time.Time
}

type clockKeyType string

// clockKey is the key we use to associate a clock with a Context.
const clockKey = clockKeyType("clock")

// Now returns the current time for the clock associated with the given context,
// or the real current time if there is no clock associated with the context.
// Any code that needs to be tested with a mocked-out time should use
// `clock.Now()` instead of `time.Now()`.
func Now(ctx context.Context) time.Time {
	if c, ok := ctx.Value(clockKey).(clock); ok && c != nil {
		return c.Now()
	}
	return time.Now()
}

// After returns time.After() or the equivalent for the clock associated with
// the given context.
func After(ctx context.Context, d time.Duration) <-chan time.Time {
	if c, ok := ctx.Value(clockKey).(clock); ok && c != nil {
		return c.After(d)
	}
	return time.After(d)
}

// NewContext returns a new context with the given clock attached.
//
// This should generally only be used in tests; production code should always
// use the real time.
func NewContext(ctx context.Context, c clock) context.Context {
	return context.WithValue(ctx, clockKey, c)
}

type timer struct {
	endTime time.Time
	ch      chan time.Time
}

// FakeClock provides support for mocking the current time in tests.
type FakeClock struct {
	mu            sync.Mutex
	now           time.Time
	pendingTimers []*timer
	afterCalled   chan struct{}
}

func NewFakeClock() *FakeClock {
	return &FakeClock{now: time.Now(), afterCalled: make(chan struct{})}
}

func (c *FakeClock) Now() time.Time {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.now
}

func (c *FakeClock) After(d time.Duration) <-chan time.Time {
	c.mu.Lock()
	defer c.mu.Unlock()
	t := &timer{c.now.Add(d), make(chan time.Time, 1)}
	c.pendingTimers = append(c.pendingTimers, t)
	select {
	case <-c.afterCalled: // Channel already closed.
	default:
		close(c.afterCalled)
	}
	return t.ch
}

func (c *FakeClock) Advance(d time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.now = c.now.Add(d)

	// Notify timers that the time has changed.
	var pendingTimers []*timer
	for _, t := range c.pendingTimers {
		if !c.now.Before(t.endTime) {
			t.ch <- c.now
		} else {
			pendingTimers = append(pendingTimers, t)
		}
	}
	c.pendingTimers = pendingTimers
}

// AfterCalledChan returns the channel to wait for the clock's timer to be set from a
// call to After().
func (c *FakeClock) AfterCalledChan() <-chan struct{} {
	return c.afterCalled
}
