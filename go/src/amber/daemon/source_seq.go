// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"os"
	"sync"
	"time"
)

// SourceKeeper wraps a Source and performs admission control for operations on
// the Source. This prevents concurrent network operations from occurring in
// some cases.
type SourceKeeper struct {
	src Source
	mu  *sync.Mutex
}

func NewSourceKeeper(src Source) *SourceKeeper {
	return &SourceKeeper{src: src, mu: &sync.Mutex{}}
}

func (k *SourceKeeper) AvailableUpdates(pkgs []*Package) (map[Package]Package, error) {
	k.mu.Lock()
	defer k.mu.Unlock()
	return k.src.AvailableUpdates(pkgs)
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
