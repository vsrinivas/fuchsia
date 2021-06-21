// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package clock

import (
	"context"
	"time"
)

type clock interface {
	Now() time.Time
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

// NewContext returns a new context with the given clock attached.
//
// This should generally only be used in tests; production code should always
// use the real time.
func NewContext(ctx context.Context, c clock) context.Context {
	return context.WithValue(ctx, clockKey, c)
}

// FakeClock provides support for mocking the current time in tests.
type FakeClock struct {
	now time.Time
}

func NewFakeClock() *FakeClock {
	return &FakeClock{now: time.Now()}
}

func (c *FakeClock) Now() time.Time {
	return c.now
}

func (c *FakeClock) Advance(d time.Duration) {
	c.now = c.now.Add(d)
}
