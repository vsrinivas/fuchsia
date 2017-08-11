// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"log"
	"os"
	"sync"
	"time"
)

// SourceKeeper wraps a Source and performs admission control for operations on
// the Source. This prevents concurrent network operations from occurring in
// some cases.
type SourceKeeper struct {
	src  Source
	mu   *sync.Mutex
	last time.Time
}

// TODO(jmatt) include a notion of when we can retry
var ErrRateExceeded = fmt.Errorf("Source rate limited exceeded, try back later")

func NewSourceKeeper(src Source) *SourceKeeper {
	return &SourceKeeper{
		src:  src,
		mu:   &sync.Mutex{},
		last: time.Now().Add(0 - src.CheckInterval()),
	}
}

func (k *SourceKeeper) AvailableUpdates(pkgs []*Package) (map[Package]Package, error) {
	k.mu.Lock()
	defer k.mu.Unlock()
	n := time.Now().Sub(k.last)
	if n < k.CheckInterval() {
		log.Printf("Query rate exceeded %d \n", n)
		return nil, ErrRateExceeded
	}
	k.last = time.Now()
	r, e := k.src.AvailableUpdates(pkgs)
	return r, e
}

func (k *SourceKeeper) FetchPkg(pkg *Package) (*os.File, error) {
	k.mu.Lock()
	defer k.mu.Unlock()
	return k.src.FetchPkg(pkg)
}

func (k *SourceKeeper) CheckInterval() time.Duration {
	return k.src.CheckInterval()
}

func (k *SourceKeeper) Equals(s Source) bool {
	return k.src.Equals(s)
}
