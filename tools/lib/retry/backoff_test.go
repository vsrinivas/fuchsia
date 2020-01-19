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

func TestMaxTriesBackoff(t *testing.T) {
	backoff := WithMaxRetries(&ZeroBackoff{}, 10)
	backoff.Reset()
	for i := 0; i < 10; i++ {
		if backoff.Next() != 0 {
			t.Error("invalid interval")
		}
	}
	if backoff.Next() != Stop {
		t.Error("did not stop")
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
	backoff := NewExponentialBackoff(time.Second*5, time.Second*64, 2)
	testBackoff := func() {
		val := backoff.Next()
		if val < 5*time.Second || val > 15*time.Second {
			t.Errorf("expecting backoff between 5 to 15 secs, got %v", val)
		}
		val = backoff.Next()
		if val < 10*time.Second || val > 25*time.Second {
			t.Errorf("expecting backoff between 10 to 25 secs, got %v", val)
		}
		val = backoff.Next()
		if val < 20*time.Second || val > 35*time.Second {
			t.Errorf("expecting backoff between 20 to 35 secs, got %v", val)
		}
		val = backoff.Next()
		if val < 40*time.Second || val > 55*time.Second {
			t.Errorf("expecting backoff between 40 to 55 secs, got %v", val)
		}
		val = backoff.Next()
		if val != 64*time.Second {
			t.Errorf("expecting backoff of 64 secs, got %v", val)
		}
	}
	testBackoff()
	// Reset and test again
	backoff.Reset()
	testBackoff()
}
