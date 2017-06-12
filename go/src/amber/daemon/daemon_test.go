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

type testTicker struct {
	i    time.Duration
	last time.Time
	C    chan time.Time
}

func testBuildTicker(d time.Duration, tickerGroup *sync.WaitGroup, mu *sync.Mutex) (*time.Ticker, testTicker) {
	mu.Lock()
	defer mu.Unlock()
	defer tickerGroup.Done()

	c := make(chan time.Time)
	t := time.NewTicker(d)
	t.C = c

	tt := testTicker{i: d, last: time.Now(), C: c}
	return t, tt
}

func (t *testTicker) makeTick() {
	t.last = t.last.Add(t.i)
	t.C <- t.last
}

func processPackage(update *Package, orig *Package, src Source, pkgs *PackageSet) error {
	pkgs.Replace(orig, update, false)
	_, err := src.FetchPkg(update)
	return err
}

// TestDaemon tests daemon.go with a fake package source. The test runs for ~30
// seconds.
func TestDaemon(t *testing.T) {
	tickers := []testTicker{}
	muTickers := sync.Mutex{}
	tickerGroup := sync.WaitGroup{}

	newTicker = func(d time.Duration) *time.Ticker {
		t, tt := testBuildTicker(d, &tickerGroup, &muTickers)
		tickers = append(tickers, tt)
		return t
	}

	tickerGroup.Add(2)

	sources := createTestSrcs()
	pkgSet := createMonitorPkgs()

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

func TestOneShot(t *testing.T) {
	// run the test a good number of times to try to catch rac-y failures
	for i := 0; i < 25; i++ {
		testDaemonOneShot(t)
	}
}

func testDaemonOneShot(t *testing.T) {
	tickers := []testTicker{}
	muTickers := sync.Mutex{}
	tickerGroup := sync.WaitGroup{}

	newTicker = func(d time.Duration) *time.Ticker {
		t, tt := testBuildTicker(d, &tickerGroup, &muTickers)
		tickers = append(tickers, tt)
		return t
	}

	tSrc := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Second * 3}
	tickerGroup.Add(1)
	monitoredPkgs := createMonitorPkgs()
	oneShotPkgsA := createOneShotPkgsA()
	oneShotPkgsB := createOneShotPkgsB()

	d := NewDaemon(monitoredPkgs, processPackage)
	d.AddSource(&tSrc)
	tickerGroup.Wait()
	// protect against improper test rewrites
	if len(tickers) != 1 {
		t.Errorf("Unexpected number of tickers!", len(tickers))
	}

	d.GetUpdates(oneShotPkgsA)
	tickers[0].makeTick()
	d.GetUpdates(oneShotPkgsB)
	// allow a brief pause for requests to start before we shut down
	// otherwise the stop request beats the GetUpdates request
	time.Sleep(5 * time.Millisecond)

	d.CancelAll()
	// verify a single request for the one-shot pkgs and a single
	// request for the monitored pkgs
	verify(t, &tSrc, oneShotPkgsA, 1)
	verify(t, &tSrc, oneShotPkgsB, 1)
	verify(t, &tSrc, monitoredPkgs, 2)
}

func createOneShotPkgsA() *PackageSet {
	pkgSet := NewPackageSet()
	pkgSet.Add(&Package{Name: "one", Version: "18facd43"})
	pkgSet.Add(&Package{Name: "two", Version: "2ade0092"})
	pkgSet.Add(&Package{Name: "three", Version: "34a077fe"})
	return pkgSet
}

func createOneShotPkgsB() *PackageSet {
	pkgSet := NewPackageSet()
	pkgSet.Add(&Package{Name: "four", Version: "18facd43"})
	pkgSet.Add(&Package{Name: "five", Version: "2ade0092"})
	pkgSet.Add(&Package{Name: "six", Version: "34a077fe"})
	return pkgSet
}

func createMonitorPkgs() *PackageSet {
	pkgSet := NewPackageSet()
	pkgSet.Add(&Package{Name: "email", Version: "23af90ee"})
	pkgSet.Add(&Package{Name: "video", Version: "f2b8006c"})
	pkgSet.Add(&Package{Name: "search", Version: "fa08207e"})
	return pkgSet
}

func createTestSrcs() []*testSrc {
	tSrc := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Second * 3}
	tSrc2 := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Second * 5}
	return []*testSrc{&tSrc, &tSrc2}
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
