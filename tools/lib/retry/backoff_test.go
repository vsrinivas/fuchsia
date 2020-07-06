// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package retry

import (
	"testing"
	"time"
)

func TestZeroBackoff(t *testing.T) {
	backoff := ZeroBackoff{}
	backoff.Reset()
	if backoff.Next() != 0 {
		t.Error("invalid interval")
	}
}

func TestConstantBackoff(t *testing.T) {
	backoff := NewConstantBackoff(time.Second)
	backoff.Reset()
	if backoff.Next() != time.Second {
		t.Error("invalid interval")
	}
}

func TestMaxAttemptsBackoff(t *testing.T) {
	backoff := WithMaxAttempts(&ZeroBackoff{}, 10)
	backoff.Reset()
	for i := 0; i < 9; i++ {
		if backoff.Next() != 0 {
			t.Errorf("WithMaxAttempts stopped unexpectedly after %d attempts", i+1)
		}
	}
	if backoff.Next() != Stop {
		t.Error("did not stop")
	}

	backoff = WithMaxAttempts(&ZeroBackoff{}, 0)
	// Three is an arbitrary number, but should be enough to validate that this
	// doesn't stop early.
	for i := 0; i < 3; i++ {
		if backoff.Next() != 0 {
			t.Error("setting maxAttempts to 0 should retry indefinitely, but stopped")
		}
	}
}

func TestMaxDurationBackoff(t *testing.T) {
	c := &fakeClock{t: time.Now()}
	backoff := &maxDurationBackoff{backOff: &ZeroBackoff{}, maxDuration: 10 * time.Second, c: c}
	backoff.Reset()
	if backoff.Next() != 0 {
		t.Error("invalid interval")
	}

	c.Tick(9 * time.Second)
	if backoff.Next() != 0 {
		t.Error("invalid interval")
	}

	c.Tick(1 * time.Second)
	if backoff.Next() != Stop {
		t.Error("did not stop")
	}
}

func TestExponentialBackoff(t *testing.T) {
	multiplier := 2.0
	maxInterval := 32 * time.Second
	backoff := NewExponentialBackoff(500*time.Millisecond, maxInterval, multiplier)
	testBackoff := func() {
		low := 250 * time.Millisecond
		high := 750 * time.Millisecond
		for low < maxInterval {
			val := backoff.Next()
			if val < low || val > high {
				t.Errorf("expecting backoff between %s and %s, got %s", low, high, val)
			}
			low *= time.Duration(multiplier)
			high *= time.Duration(multiplier)
		}
		val := backoff.Next()
		if val != maxInterval {
			t.Errorf("expecting backoff of %s secs, got %s", maxInterval, val)
		}
	}
	testBackoff()
	// Reset and test again
	backoff.Reset()
	testBackoff()
}

func TestNoRetries(t *testing.T) {
	backoff := NoRetries()
	if backoff.Next() != Stop {
		t.Errorf("expected NoRetries backoff to not retry")
	}
	backoff.Reset()
	if backoff.Next() != Stop {
		t.Errorf("expected NoRetries backoff to not retry after a reset")
	}
}
