// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import "time"

// TickGenerator generates ticks. The frequency with which ticks are created is
// determined by the delay function passed to NewTickGenerator. Something that
// wants to consume ticks can call AwaitTick. The next tick will be delivered
// at the greater of the next interval specified by the delay function and when
// AwaitTick is called. A single tick will queue if the tick consumer is busy.
// Ticks may also be generated outside the normal schedule by calling
// GenerateTick. A tick is delivered to a single waiter.
//
// A typical scenario for using a TickGenerator for something to call Run in
// a Go routine and pass it to a tick consumer. The consumer then calls
// AwaitTick and after doing whatever work it wants calls AwaitTick again. The
// next tick arrives based on the delay returned by the delay function.
// Something wanting work to be done immediately might call GenerateTick.
// Eventually the Ticker may no longer be needed and Done should be called.
// After this points ticks can still be generated with GenerateTick, but time-
// sequenced ticks will no longer be produced.
type TickGenerator struct {
	tick   chan struct{}
	finish chan struct{}
	delay  func(time.Duration) time.Duration
}

func NewTickGenerator(d func(time.Duration) time.Duration) *TickGenerator {
	return &TickGenerator{
		tick:   make(chan struct{}, 1),
		finish: make(chan struct{}),
		delay:  d,
	}
}

// Done results in the Run routine exiting. After this point time-delayed ticks
// will not be produced, but ones can be made manually with GenerateTick.
func (t *TickGenerator) Done() {
	close(t.finish)
}

// Run generates ticks with a delay pattern defined by the delay routine
// the instance is given. Ticks can be generated outside this schedule by
// calling GenerateTick. The ticks are generated without concern for how long
// it takes to consume them, but at most one tick will be queued.
func (t *TickGenerator) Run() {
	// initial sleep time
	s := 0 * time.Second // TODO can this just be '0'?

	for {
		select {
		case <-time.After(s):
			t.GenerateTick()
			s = t.delay(s)
			if s < 0 {
				close(t.tick)
				return
			}
		case <-t.finish:
			close(t.tick)
			return
		}
	}
}

// AwaitTick returns when something calls GenerateTick or the next iteration
// of the run loop executes.
func (t *TickGenerator) AwaitTick() bool {
	_, ok := <-t.tick
	return ok
}

// GenerateTick generates a new tick. If a tick is already pending another tick
// will not be generated.
func (t *TickGenerator) GenerateTick() {
	select {
	case t.tick <- struct{}{}:
	default:
	}
}
