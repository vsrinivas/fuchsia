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

// ExponentialBackoff is a policy that increase the delay exponentially.
type ExponentialBackoff struct {
	initialInterval time.Duration
	maxInterval     time.Duration
	multiplier      float64
	iteration       int
	randObj         *rand.Rand
}

// NewExponentialBackoff returns a new ExponentialBackoff object.
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
	// next is sec in float64
	next := float64(e.initialInterval)/float64(time.Second)*math.Pow(e.multiplier, float64(e.iteration)) + 10*e.randObj.Float64()
	if next > float64(e.maxInterval)/float64(time.Second) {
		return e.maxInterval
	}
	e.iteration++
	return time.Duration(float64(time.Second) * next)
}
