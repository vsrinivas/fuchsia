// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"encoding/json"
	"fmt"
	"os"
	"time"

	tuf "github.com/flynn/go-tuf/client"
	tuf_data "github.com/flynn/go-tuf/data"
)

type RemoteStoreError struct {
	error
}

type IOError struct {
	error
}

func NewTUFClient(url string, path string) (*tuf.Client, tuf.LocalStore, error) {
	tufStore, err := tuf.FileLocalStore(path)
	if err != nil {
		return nil, nil, IOError{fmt.Errorf("amber: couldn't open datastore %s\n", err)}
	}

	server, err := tuf.HTTPRemoteStore(url, nil)
	if err != nil {
		return nil, nil, RemoteStoreError{fmt.Errorf("amber: server address not understood %s\n", err)}
	}

	return tuf.NewClient(tufStore, server), tufStore, nil
}

func InitNewTUFClient(url string, path string, keys []*tuf_data.Key, ticker *TickGenerator) (*tuf.Client, tuf.LocalStore, error) {
	client, store, err := NewTUFClient(url, path)

	defer ticker.Done()
	if err != nil {
		return nil, nil, err
	}

	needs, err := NeedsInit(store)
	if err != nil {
		return nil, nil, err
	}

	if needs {
		if err = InitClient(client, keys, ticker); err != nil {
			return nil, nil, err
		}
	}

	return client, store, nil
}

func LoadKeys(path string) ([]*tuf_data.Key, error) {
	f, err := os.Open(path)
	defer f.Close()
	if err != nil {
		return nil, err
	}

	var keys []*tuf_data.Key
	err = json.NewDecoder(f).Decode(&keys)
	return keys, err
}

func NeedsInit(s tuf.LocalStore) (bool, error) {
	meta, err := s.GetMeta()
	if err != nil {
		return false, err
	}

	_, ok := meta["root.json"]
	return !ok, nil
}

func InitClient(c *tuf.Client, keys []*tuf_data.Key, clock *TickGenerator) error {
	for {
		clock.AwaitTick()

		err := c.Init(keys, len(keys))
		if err == nil {
			break
		}
	}

	return nil
}

func InitBackoff(cur time.Duration) time.Duration {
	minDelay := 1 * time.Second
	maxStep := 30 * time.Second
	maxDelay := 5 * time.Minute

	if cur < time.Second {
		return minDelay
	}

	if cur > maxStep {
		cur += maxStep
	} else {
		cur += cur
	}

	if cur > maxDelay {
		cur = maxDelay
	}
	return cur
}

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
				return
			}
		case <-t.finish:
			return
		}
	}
}

// AwaitTick returns when something calls GenerateTick or the next iteration
// of the run loop executes.
func (t *TickGenerator) AwaitTick() {
	<-t.tick
}

// GenerateTick generates a new tick. If a tick is already pending another tick
// will not be generated.
func (t *TickGenerator) GenerateTick() {
	select {
	case t.tick <- struct{}{}:
	default:
	}
}
