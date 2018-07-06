// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	"amber/pkg"
	"amber/source"

	"fidl/fuchsia/amber"
)

// SourceKeeper wraps a Source and performs admission control for operations on
// the Source. This prevents concurrent network operations from occurring in
// some cases.
type SourceKeeper struct {
	src  source.Source
	mu   *sync.Mutex
	hist []time.Time
}

// TODO(jmatt) include a notion of when we can retry
var ErrRateExceeded = fmt.Errorf("Source rate limited exceeded, try back later")

func NewSourceKeeper(src source.Source) *SourceKeeper {
	return &SourceKeeper{
		src:  src,
		mu:   &sync.Mutex{},
		hist: []time.Time{},
	}
}

func (k *SourceKeeper) GetId() string {
	return k.src.GetId()
}

func (k *SourceKeeper) GetConfig() *amber.SourceConfig {
	return k.src.GetConfig()
}

func (k *SourceKeeper) GetHttpClient() *http.Client {
	return k.src.GetHttpClient()
}

func (k *SourceKeeper) Login() (*amber.DeviceCode, error) {
	return k.src.Login()
}

func (k *SourceKeeper) AvailableUpdates(pkgs []*pkg.Package) (map[pkg.Package]pkg.Package, error) {
	k.mu.Lock()
	defer k.mu.Unlock()

	slicePt := len(k.hist)
	interval := k.CheckInterval()
	for idx, val := range k.hist {
		if interval == 0 || time.Since(val) < interval {
			slicePt = idx
			break
		}
	}

	if slicePt > 0 {
		nlen := len(k.hist) - slicePt
		nhist := make([]time.Time, nlen)
		copy(nhist, k.hist[slicePt:])
		k.hist = nhist
	}

	if k.CheckLimit() > 0 && uint64(len(k.hist)+1) > k.CheckLimit() {
		log.Println("Query rate exceeded")
		return nil, ErrRateExceeded
	}

	k.hist = append(k.hist, time.Now())
	r, e := k.src.AvailableUpdates(pkgs)
	return r, e
}

func (k *SourceKeeper) FetchPkg(pkg *pkg.Package) (*os.File, error) {
	k.mu.Lock()
	defer k.mu.Unlock()
	return k.src.FetchPkg(pkg)
}

func (k *SourceKeeper) CheckInterval() time.Duration {
	return k.src.CheckInterval()
}

func (k *SourceKeeper) Equals(s source.Source) bool {
	return k.src.Equals(s)
}

func (k *SourceKeeper) CheckLimit() uint64 {
	return k.src.CheckLimit()
}

func (k *SourceKeeper) Save() error {
	return k.src.Save()
}

func (k *SourceKeeper) DeleteConfig() error {
	return k.src.DeleteConfig()
}

func (k *SourceKeeper) Delete() error {
	return k.src.Delete()
}

func (k *SourceKeeper) Close() {
	k.src.Close()
}
