// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"math/rand"
	"os"
	"sync"
	"testing"
	"time"
)

var letters = []rune("1234567890abcdef")

func randSeq(n int) string {
	rand.Seed(time.Now().UnixNano())
	runeLen := len(letters)
	b := make([]rune, n)
	for i := range b {
		b[i] = letters[rand.Intn(runeLen)]
	}
	return string(b)
}

type testSrc struct {
	mu         sync.Mutex
	UpdateReqs map[string]int
	getReqs    map[Package]*struct{}
	interval   time.Duration
}

func (t *testSrc) AvailableUpdates(pkgs []*Package) (map[Package]Package, error) {
	t.mu.Lock()

	updates := make(map[Package]Package)
	for _, p := range pkgs {
		t.UpdateReqs[p.Name] = t.UpdateReqs[p.Name] + 1
		up := Package{Name: p.Name, Version: randSeq(6)}
		t.getReqs[up] = &struct{}{}
		updates[*p] = up
	}

	t.mu.Unlock()
	return updates, nil
}

func (t *testSrc) FetchPkg(pkg *Package) (*os.File, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	if _, ok := t.getReqs[*pkg]; !ok {
		fmt.Println("ERROR: unknown update pkg requested")
		return nil, ErrNoUpdateContent
	}

	delete(t.getReqs, *pkg)
	return nil, nil
}

func (t *testSrc) CheckInterval() time.Duration {
	return t.interval
}

func (t *testSrc) Equals(o Source) bool {
	switch p := o.(type) {
	case *testSrc:
		return t == p
	default:
		return false
	}
}

func processPackage(pkg *Package, src Source) error {
	_, err := src.FetchPkg(pkg)
	return err
}

var (
	tickers     = []testTicker{}
	muTickers   sync.Mutex
	tickerGroup sync.WaitGroup
)

type testTicker struct {
	i    time.Duration
	last time.Time
	C    chan time.Time
}

func testBuildTicker(d time.Duration) *time.Ticker {
	muTickers.Lock()
	defer muTickers.Unlock()
	defer tickerGroup.Done()

	c := make(chan time.Time)
	t := time.NewTicker(d)
	t.C = c

	tt := testTicker{i: d, last: time.Now(), C: c}
	tickers = append(tickers, tt)
	return t
}

func (t *testTicker) makeTick() {
	t.last = t.last.Add(t.i)
	t.C <- t.last
}

// TestDaemon tests daemon.go with a fake package source. The test runs for ~30
// seconds.
func TestDaemon(t *testing.T) {
	newTicker = testBuildTicker
	tickerGroup.Add(2)

	tSrc := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Second * 3}
	tSrc2 := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Second * 5}
	sources := []*testSrc{&tSrc, &tSrc2}
	pkgSet := NewPackageSet()
	pkgSet.Add(&Package{Name: "email", Version: "23af90ee"})
	pkgSet.Add(&Package{Name: "video", Version: "f2b8006c"})
	pkgSet.Add(&Package{Name: "search", Version: "fa08207e"})

	d := NewDaemon(pkgSet, processPackage)
	for _, src := range sources {
		d.AddSource(src)
	}

	tickerGroup.Wait()

	// protect against improper test rewrites
	if len(tickers) != 2 {
		t.Errorf("Unexpected number of tickers!", len(tickers))
	}

	sepRuns := 10
	simulRuns := 20

	// run 10 times with a slight separation
	for i := 0; i < sepRuns; i++ {
		o := i % 2
		tickers[o].makeTick()
		time.Sleep(10 * time.Millisecond)
		o++
		o = o % 2
		tickers[o].makeTick()
	}

	// run 20 times together
	for i := 0; i < simulRuns; i++ {
		o := i % 2
		tickers[o].makeTick()
		o++
		o = o % 2
		tickers[o].makeTick()
	}

	d.CancelAll()

	for _, src := range sources {
		verify(t, src, pkgSet, sepRuns+simulRuns+1)
	}
}

func verify(t *testing.T, src *testSrc, pkgs *PackageSet, runs int) {
	for _, pkg := range pkgs.Packages() {
		actRuns := src.UpdateReqs[pkg.Name]
		if actRuns != runs {
			t.Errorf("Incorrect execution count, found %d, but expected %d for %s\n", actRuns, runs, pkg.Name)
		}
	}

	if len(src.getReqs) != 0 {
		t.Errorf("Error, some pkgs were not requested!")
	}
}
