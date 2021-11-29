// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package retry

import (
	"math"
	"math/rand"
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

type maxAttemptsBackoff struct {
	backOff     Backoff
	maxAttempts uint64
	numAttempts uint64
}

func (b *maxAttemptsBackoff) Next() time.Duration {
	if b.maxAttempts > 0 {
		b.numAttempts++
		if b.numAttempts >= b.maxAttempts {
			return Stop
		}
	}
	return b.backOff.Next()
}

func (b *maxAttemptsBackoff) Reset() {
	b.numAttempts = 0
	b.backOff.Reset()
}

// WithMaxAttempts wraps a back-off which stops after |max| attempts. If the max
// is 0, then it won't apply a maximum number attempts.
func WithMaxAttempts(b Backoff, max uint64) Backoff {
	return &maxAttemptsBackoff{backOff: b, maxAttempts: max}
}

type maxDurationBackoff struct {
	backOff     Backoff
	maxDuration time.Duration
	startTime   time.Time
	c           clock
}

func (b *maxDurationBackoff) Next() time.Duration {
	if b.c.Since(b.startTime) < b.maxDuration {
		return b.backOff.Next()
	}
	return Stop
}

func (b *maxDurationBackoff) Reset() {
	b.startTime = b.c.Now()
	b.backOff.Reset()
}

// WithMaxDuration wraps a back-off which stops attempting retries after |max|
// duration.
func WithMaxDuration(b Backoff, max time.Duration) Backoff {
	return &maxDurationBackoff{backOff: b, maxDuration: max, c: &systemClock{}}
}

// ExponentialBackoff is a policy that increase the delay exponentially, with a
// small amount of randomness.
type ExponentialBackoff struct {
	initialInterval time.Duration
	maxInterval     time.Duration
	multiplier      float64
	iteration       int
	randObj         *rand.Rand
}

// NewExponentialBackoff returns a new ExponentialBackoff object.
// If maxInterval is non-positive, no maximum is imposed.
func NewExponentialBackoff(initialInterval time.Duration, maxInterval time.Duration, multiplier float64) *ExponentialBackoff {
	return &ExponentialBackoff{
		initialInterval: initialInterval,
		maxInterval:     maxInterval,
		multiplier:      multiplier,
		iteration:       0,
		randObj:         rand.New(rand.NewSource(time.Now().UnixNano())),
	}
}

func (e *ExponentialBackoff) Reset() {
	e.iteration = 0
}

func (e *ExponentialBackoff) Next() time.Duration {
	// Number of seconds to wait for the next attempt.
	seconds := float64(e.initialInterval) / float64(time.Second) * math.Pow(e.multiplier, float64(e.iteration))
	// Plus a random interval proportional to the number of seconds, with a
	// ceiling.
	seconds += math.Min(10.0, seconds/2) * e.randObj.Float64()
	next := time.Duration(seconds * float64(time.Second))
	if e.maxInterval > 0 && next > e.maxInterval {
		return e.maxInterval
	}
	e.iteration++
	return next
}

// NoRetries returns a backoff that will do a single attempt, with no retries.
func NoRetries() Backoff {
	return WithMaxAttempts(&ZeroBackoff{}, 1)
}
