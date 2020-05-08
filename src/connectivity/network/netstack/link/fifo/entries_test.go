// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fifo

import (
	"fmt"
	"math/bits"
	"testing"
)

type entries struct {
	Entries
	storage []struct{}
}

func (e *entries) addReadied(src []struct{}) int {
	if readied, sent := e.GetInFlightRange(); readied < sent {
		return copy(e.storage[readied:sent], src)
	} else {
		n := copy(e.storage[readied:], src)
		n += copy(e.storage[:sent], src[n:])
		return n
	}
}

func (e *entries) getQueued(dst []struct{}) int {
	if sent, queued := e.GetQueuedRange(); sent < queued {
		return copy(dst, e.storage[sent:queued])
	} else {
		n := copy(dst, e.storage[sent:])
		n += copy(dst[n:], e.storage[:queued])
		return n
	}
}

func (e *entries) init(depth uint16) {
	capacity := e.Entries.Init(depth)
	e.storage = make([]struct{}, capacity)

}

func TestEntries(t *testing.T) {
	var e entries

	var maxDepth uint16
	// Statically assert that all indices are the same type.
	maxDepth = e.sent
	maxDepth = e.queued
	maxDepth = e.readied

	// Use unsigned integer underflow to determine the maximum permitted depth.
	maxDepth = 0
	maxDepth--
	// The maximum permitted depth is half the range of the index type used.
	maxDepth >>= 1

	for _, depth := range []uint16{2, 50, maxDepth} {
		t.Run(fmt.Sprintf("depth=%d", depth), func(t *testing.T) {
			e.init(depth)

			if ones := bits.OnesCount16(e.capacity); ones != 1 {
				t.Fatalf("got len(storage)=%d (binary=%b) want power of two", e.capacity, e.capacity)
			}

			scratch := make([]struct{}, e.capacity)

			for _, delta := range []uint16{depth, 1, depth / 2, depth - 1, depth} {
				t.Run(fmt.Sprintf("delta=%d", delta), func(t *testing.T) {
					if e.HaveReadied() {
						t.Errorf("got HaveReadied=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
					if got, want := e.addReadied(scratch), int(e.capacity); got != want {
						t.Errorf("got addReadied=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}

					e.IncrementReadied(delta)

					if got, want := e.HaveReadied(), delta != 0; got != want {
						t.Errorf("got HaveReadied=%t want=%t; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if got, want := e.addReadied(scratch), int(e.capacity-delta); got != want {
						t.Errorf("got addReadied=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if e.HaveQueued() {
						t.Errorf("got HaveQueued=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
					if got, want := e.getQueued(scratch), 0; got != want {
						t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}

					e.IncrementQueued(delta)

					if got, want := e.HaveQueued(), delta != 0; got != want {
						t.Errorf("got HaveQueued=%t want=%t; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if e.HaveReadied() {
						t.Errorf("got HaveReadied=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
					if delta == 0 {
						if got, want := e.getQueued(scratch), int(e.capacity); got != want {
							t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
						}
						if inFlight := e.InFlight(); inFlight != 0 {
							t.Errorf("got InFlight()=%d want=zero; sent=%d queued=%d readied=%d", inFlight, e.sent, e.queued, e.readied)
						}
					} else {
						if got, want := e.getQueued(scratch), int(delta); got != want {
							t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
						}
						if got, want := e.InFlight(), e.capacity-delta; got != want {
							t.Errorf("got InFlight()=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
						}
					}

					e.IncrementSent(delta)

					if got, want := e.InFlight(), e.capacity; got != want {
						t.Errorf("got InFlight()=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if got, want := e.addReadied(scratch), int(e.capacity); got != want {
						t.Errorf("got addReadied=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if got, want := e.getQueued(scratch), 0; got != want {
						t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if e.HaveQueued() {
						t.Errorf("got HaveQueued=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
					if e.HaveReadied() {
						t.Fatalf("got HaveReadied=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
				})
			}
		})
	}
}
