// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package fifo

import (
	"fmt"
	"math/bits"
	"testing"

	"entries_gen_test/entries"
)

func TestEntries(t *testing.T) {
	var e entries.Entries

	var maxDepth uint16

	// Use unsigned integer underflow to determine the maximum permitted depth.
	maxDepth = 0
	maxDepth--
	// The maximum permitted depth is half the range of the index type used.
	maxDepth >>= 1

	for _, depth := range []uint16{2, 50, maxDepth} {
		t.Run(fmt.Sprintf("depth=%d", depth), func(t *testing.T) {
			capacity := e.Init(depth)

			if ones := bits.OnesCount16(capacity); ones != 1 {
				t.Fatalf("got len(storage)=%d (binary=%b) want power of two", capacity, capacity)
			}

			scratch := make([]struct{}, capacity)

			for _, delta := range []uint16{depth, 1, depth / 2, depth - 1, depth} {
				t.Run(fmt.Sprintf("delta=%d", delta), func(t *testing.T) {
					if e.HaveReadied() {
						t.Errorf("got HaveReadied=true want=false; %#v", e)
					}
					if got, want := e.AddReadied(scratch), int(capacity); got != want {
						t.Errorf("got addReadied=%d want=%d; %#v", got, want, e)
					}

					e.IncrementReadied(delta)

					if got, want := e.HaveReadied(), delta != 0; got != want {
						t.Errorf("got HaveReadied=%t want=%t; %#v", got, want, e)
					}
					if got, want := e.AddReadied(scratch), int(capacity-delta); got != want {
						t.Errorf("got addReadied=%d want=%d; %#v", got, want, e)
					}
					if e.HaveQueued() {
						t.Errorf("got HaveQueued=true want=false; %#v", e)
					}
					if got, want := e.GetQueued(scratch), 0; got != want {
						t.Errorf("got getQueued=%d want=%d; %#v", got, want, e)
					}

					e.IncrementQueued(delta)

					if got, want := e.HaveQueued(), delta != 0; got != want {
						t.Errorf("got HaveQueued=%t want=%t; %#v", got, want, e)
					}
					if e.HaveReadied() {
						t.Errorf("got HaveReadied=true want=false; %#v", e)
					}
					if delta == 0 {
						if got, want := e.GetQueued(scratch), int(capacity); got != want {
							t.Errorf("got getQueued=%d want=%d; %#v", got, want, e)
						}
						if inFlight := e.InFlight(); inFlight != 0 {
							t.Errorf("got InFlight()=%d want=zero; %#v", inFlight, e)
						}
					} else {
						if got, want := e.GetQueued(scratch), int(delta); got != want {
							t.Errorf("got getQueued=%d want=%d; %#v", got, want, e)
						}
						if got, want := e.InFlight(), capacity-delta; got != want {
							t.Errorf("got InFlight()=%d want=%d; %#v", got, want, e)
						}
					}

					e.IncrementSent(delta)

					if got, want := e.InFlight(), capacity; got != want {
						t.Errorf("got InFlight()=%d want=%d; %#v", got, want, e)
					}
					if got, want := e.AddReadied(scratch), int(capacity); got != want {
						t.Errorf("got addReadied=%d want=%d; %#v", got, want, e)
					}
					if got, want := e.GetQueued(scratch), 0; got != want {
						t.Errorf("got getQueued=%d want=%d; %#v", got, want, e)
					}
					if e.HaveQueued() {
						t.Errorf("got HaveQueued=true want=false; %#v", e)
					}
					if e.HaveReadied() {
						t.Fatalf("got HaveReadied=true want=false; %#v", e)
					}
				})
			}
		})
	}
}
