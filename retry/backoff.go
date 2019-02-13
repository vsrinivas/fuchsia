// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package retry

import (
	"time"
)

// Stop indicates that no more retries should be made.
const Stop time.Duration = -1

type Backoff interface {
	// Next gets the duration to wait before retrying the operation or |Stop|
	// to indicate that no retries should be made.
	Next() time.Duration

	// Reset resets to initial state.
	Reset()
}

// ZeroBackoff is a fixed policy whose back-off time is always zero, meaning
// that the operation is retried immediately without waiting.
type ZeroBackoff struct{}

func (b *ZeroBackoff) Reset() {}

func (b *ZeroBackoff) Next() time.Duration { return 0 }

// ConstantBackoff is a fixed policy that always returns the same backoff delay.
type ConstantBackoff struct {
	interval time.Duration
}

func (b *ConstantBackoff) Reset() {}

func (b *ConstantBackoff) Next() time.Duration { return b.interval }

func NewConstantBackoff(d time.Duration) *ConstantBackoff {
	return &ConstantBackoff{interval: d}
}

type maxTriesBackoff struct {
	backOff  Backoff
	maxTries uint64
	numTries uint64
}

func (b *maxTriesBackoff) Next() time.Duration {
	if b.maxTries > 0 {
		if b.maxTries <= b.numTries {
			return Stop
		}
		b.numTries++
	}
	return b.backOff.Next()
}

func (b *maxTriesBackoff) Reset() {
	b.numTries = 0
	b.backOff.Reset()
}

// WithMaxRetries wraps a back-off which stops after |max| retries.
func WithMaxRetries(b Backoff, max uint64) Backoff {
	return &maxTriesBackoff{backOff: b, maxTries: max}
}
