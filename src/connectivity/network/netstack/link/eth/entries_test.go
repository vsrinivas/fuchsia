// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"fmt"
	"math/bits"
	"testing"
)

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

	for _, depth := range []uint16{1, 50, maxDepth} {
		t.Run(fmt.Sprintf("depth=%d", depth), func(t *testing.T) {
			e.init(uint32(depth))

			if ones := bits.OnesCount(uint(len(e.storage))); ones != 1 {
				t.Fatalf("got len(storage)=%d (binary=%b) want power of two", len(e.storage), len(e.storage))
			}

			scratch := make([]FifoEntry, len(e.storage)<<1)

			for _, delta := range []uint16{1, depth / 2, depth - 1, depth} {
				t.Run(fmt.Sprintf("delta=%d", delta), func(t *testing.T) {
					if e.haveReadied() {
						t.Errorf("got haveReadied=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
					if got, want := e.addReadied(scratch), len(e.storage); got != want {
						t.Errorf("got addReadied=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}

					e.incrementReadied(delta)

					if got, want := e.haveReadied(), delta != 0; got != want {
						t.Errorf("got haveReadied=%t want=%t; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if got, want := e.addReadied(scratch), len(e.storage)-int(delta); got != want {
						t.Errorf("got addReadied=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if e.haveQueued() {
						t.Errorf("got haveQueued=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
					if got, want := e.getQueued(scratch), len(e.storage); got != want {
						t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}

					e.incrementQueued(delta)

					if got, want := e.haveQueued(), delta != 0; got != want {
						t.Errorf("got haveQueued=%t want=%t; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if e.haveReadied() {
						t.Errorf("got haveReadied=true want=false; sent=%d queued=%d readied=%d", e.sent, e.queued, e.readied)
					}
					if delta == 0 {
						if got, want := e.getQueued(scratch), len(e.storage); got != want {
							t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
						}
						if inFlight := e.inFlight(); inFlight != 0 {
							t.Errorf("got inFlight()=%d want=zero; sent=%d queued=%d readied=%d", inFlight, e.sent, e.queued, e.readied)
						}
					} else {
						if got, want := e.getQueued(scratch), int(delta); got != want {
							t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
						}
						if inFlight := e.inFlight(); inFlight == 0 {
							t.Errorf("got inFlight()=%d want=nonzero; sent=%d queued=%d readied=%d", inFlight, e.sent, e.queued, e.readied)
						}
					}

					e.incrementSent(delta)

					if got, want := e.inFlight(), uint16(0); got != want {
						t.Errorf("got inFlight()=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if got, want := e.addReadied(scratch), len(e.storage); got != want {
						t.Errorf("got addReadied=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
					if got, want := e.getQueued(scratch), len(e.storage); got != want {
						t.Errorf("got getQueued=%d want=%d; sent=%d queued=%d readied=%d", got, want, e.sent, e.queued, e.readied)
					}
				})
			}
		})
	}
}
