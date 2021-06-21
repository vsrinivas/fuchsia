// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package clock

import (
	"context"
	"testing"
	"time"
)

func TestClock(t *testing.T) {
	// We use this function to guarantee that the real time.Now() function will
	// return a different time before and after a call. (In practice it's
	// extremely unlikely, if not impossible, for subsequent time.Now() calls to
	// return the same value, but can't hurt to be safe.) It's not ideal that we
	// actually sleep since it slows down the test, but there's no other way to
	// do what we need, and the test should still be deterministic because Go
	// uses monotonic time to compare Times.
	sleep := func() {
		time.Sleep(10 * time.Nanosecond)
	}

	t.Run("real time", func(t *testing.T) {
		ctx := context.Background()
		startTime := time.Now()

		sleep()

		// After sleeping, the time should be later.
		now := Now(ctx)
		if !now.After(startTime) {
			t.Errorf("Expected clock.Now() to return the real time (later than %q) but got %q", startTime, now)
		}
	})

	t.Run("faked time", func(t *testing.T) {
		fakeClock := NewFakeClock()
		startTime := fakeClock.Now()
		ctx := NewContext(context.Background(), fakeClock)

		sleep()

		// After sleeping, the time should NOT be any later because it's faked.
		now := Now(ctx)
		if !now.Equal(startTime) {
			t.Fatalf("Wrong time from clock.Now(): expected %q, got %q", startTime, now)
		}

		// But the time SHOULD be later after advancing the fake clock.
		diff := time.Minute
		fakeClock.Advance(diff)
		expectedNow := startTime.Add(diff)
		now = Now(ctx)
		if !now.Equal(expectedNow) {
			t.Fatalf("Wrong time from clock.Now(): expected %q, got %q", now, expectedNow)
		}
	})
}
