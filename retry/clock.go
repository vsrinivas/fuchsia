// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package retry

import "time"

type clock interface {
	Now() time.Time
	Since(time.Time) time.Duration
}

type fakeClock struct {
	t time.Time
}

func (c fakeClock) Now() time.Time {
	return c.t
}

func (c fakeClock) Since(startTime time.Time) time.Duration {
	return c.t.Sub(startTime)
}

func (c *fakeClock) Tick(d time.Duration) {
	c.t = c.t.Add(d)
}

type systemClock struct{}

func (c systemClock) Now() time.Time {
	return time.Now()
}

func (c systemClock) Since(startTime time.Time) time.Duration {
	return time.Since(startTime)
}
