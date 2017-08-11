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
	pkgs       map[string]struct{}
	replyDelay time.Duration
}

func (t *testSrc) AvailableUpdates(pkgs []*Package) (map[Package]Package, error) {
	t.mu.Lock()
	time.Sleep(t.replyDelay)
	updates := make(map[Package]Package)
	for _, p := range pkgs {
		if _, ok := t.pkgs[p.Name]; !ok {
			continue
		}
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

func processPackage(r *GetResult, pkgs *PackageSet) error {
	if r.Err != nil {
		return r.Err
	}
	pkgs.Replace(&r.Orig, &r.Update, false)
	return nil
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

	tickerGroup.Add(1)

	sources := createTestSrcs()
	pkgSet := createMonitorPkgs()

	d := NewDaemon(pkgSet, processPackage)
	for _, src := range sources {
		d.AddSource(src)
	}

	tickerGroup.Wait()

	// protect against improper test rewrites
	if len(tickers) != 1 {
		t.Errorf("Unexpected number of tickers!", len(tickers))
	}

	// run 10 times with a slight separation so as not to exceed the
	// throttle rate
	runs := 10
	for i := 0; i < runs; i++ {
		time.Sleep(10 * time.Millisecond)
		tickers[0].makeTick()
	}

	d.CancelAll()

	verifyReqCount(t, sources, pkgSet, runs+1)
}

func TestGetRequest(t *testing.T) {
	emailPkg := Package{Name: "email", Version: "23af90ee"}
	videoPkg := Package{Name: "video", Version: "f2b8006c"}
	srchPkg := Package{Name: "search", Version: "fa08207e"}

	// create some test sources where neither has the full pkg set and
	// they overlap
	pkgs := make(map[string]struct{})
	pkgs[emailPkg.Name] = struct{}{}
	pkgs[videoPkg.Name] = struct{}{}
	srcRateLimit := time.Millisecond * 1
	tSrc := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: srcRateLimit,
		pkgs:     pkgs}

	pkgs = make(map[string]struct{})
	pkgs[videoPkg.Name] = struct{}{}
	pkgs[srchPkg.Name] = struct{}{}
	tSrc2 := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: srcRateLimit,
		pkgs:     pkgs}
	sources := []*testSrc{&tSrc, &tSrc2}

	tickers := []testTicker{}
	muTickers := sync.Mutex{}
	tickerGroup := sync.WaitGroup{}

	newTicker = func(d time.Duration) *time.Ticker {
		t, tt := testBuildTicker(d, &tickerGroup, &muTickers)
		tickers = append(tickers, tt)
		return t
	}

	tickerGroup.Add(1)

	d := NewDaemon(NewPackageSet(), processPackage)
	for _, src := range sources {
		d.AddSource(src)
	}

	tickerGroup.Wait()

	pkgSet := NewPackageSet()
	pkgSet.Add(&emailPkg)
	pkgSet.Add(&videoPkg)
	pkgSet.Add(&srchPkg)
	updateRes := d.GetUpdates(pkgSet)
	verifyGetResults(t, pkgSet, updateRes)

	time.Sleep(srcRateLimit * 2)
	pkgSet = NewPackageSet()
	pkgSet.Add(&videoPkg)
	updateRes = d.GetUpdates(pkgSet)
	verifyGetResults(t, pkgSet, updateRes)

	d.CancelAll()
}

func TestRateLimit(t *testing.T) {
	srcRateLimit := 20 * time.Millisecond
	tSrc := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: srcRateLimit,
		pkgs:     make(map[string]struct{})}
	wrapped := NewSourceKeeper(&tSrc)
	dummy := []*Package{&Package{Name: "None", Version: "aaaaaa"}}

	if _, err := wrapped.AvailableUpdates(dummy); err == ErrRateExceeded {
		t.Errorf("Initial request was rate limited unexpectedly.\n")
	}

	if _, err := wrapped.AvailableUpdates(dummy); err != ErrRateExceeded {
		t.Errorf("Request was not rate limited\n")
	}

	time.Sleep(srcRateLimit)
	if _, err := wrapped.AvailableUpdates(dummy); err == ErrRateExceeded {
		t.Errorf("Rate-allowed request failed.\n")
	}
}

func TestRequestCollapse(t *testing.T) {
	pkgSet := NewPackageSet()
	emailPkg := Package{Name: "email", Version: "23af90ee"}
	videoPkg := Package{Name: "video", Version: "f2b8006c"}
	srchPkg := Package{Name: "search", Version: "fa08207e"}
	pkgSet.Add(&emailPkg)
	pkgSet.Add(&videoPkg)
	pkgSet.Add(&srchPkg)

	// create some test sources where neither has the full pkg set and
	// they overlap
	pkgs := make(map[string]struct{})
	pkgs[emailPkg.Name] = struct{}{}
	pkgs[videoPkg.Name] = struct{}{}
	srcRateLimit := time.Millisecond
	replyDelay := 20 * time.Millisecond
	tSrc := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: srcRateLimit,
		pkgs:     pkgs}

	pkgs = make(map[string]struct{})
	pkgs[videoPkg.Name] = struct{}{}
	pkgs[srchPkg.Name] = struct{}{}
	tSrc2 := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: srcRateLimit,
		pkgs:     pkgs}
	sources := []*testSrc{&tSrc, &tSrc2}

	tickers := []testTicker{}
	muTickers := sync.Mutex{}
	tickerGroup := sync.WaitGroup{}

	newTicker = func(d time.Duration) *time.Ticker {
		t, tt := testBuildTicker(d, &tickerGroup, &muTickers)
		tickers = append(tickers, tt)
		return t
	}

	tickerGroup.Add(1)

	d := NewDaemon(NewPackageSet(), processPackage)
	for _, src := range sources {
		// introduce a reply delay so we can make sure to run
		// simultaneously
		src.replyDelay = replyDelay
		d.AddSource(src)
	}

	tickerGroup.Wait()

	// we expect to generate only one request, since whichever arrives
	// second should just subscribe to the results of the first
	go d.GetUpdates(pkgSet)
	time.Sleep(2 * srcRateLimit)
	updateRes := d.GetUpdates(pkgSet)
	verifyReqCount(t, sources, pkgSet, 1)
	verifyGetResults(t, pkgSet, updateRes)

	// verify that if we do two more requests sequentially that the total
	// request found is as expected
	d.GetUpdates(pkgSet)
	time.Sleep(srcRateLimit)
	d.GetUpdates(pkgSet)
	verifyReqCount(t, sources, pkgSet, 3)

	pkgSetA := NewPackageSet()
	pkgSetA.Add(&emailPkg)
	pkgSetA.Add(&srchPkg)
	pkgSetB := NewPackageSet()
	pkgSetB.Add(&videoPkg)
	pkgSetB.Add(&srchPkg)
	go d.GetUpdates(pkgSetA)
	time.Sleep(srcRateLimit * 2)
	res := d.GetUpdates(pkgSetB)
	verifyReqCount(t, sources, pkgSet, 4)
	verifyGetResults(t, pkgSetB, res)

	d.CancelAll()
}

func createMonitorPkgs() *PackageSet {
	pkgSet := NewPackageSet()
	pkgSet.Add(&Package{Name: "email", Version: "23af90ee"})
	pkgSet.Add(&Package{Name: "video", Version: "f2b8006c"})
	pkgSet.Add(&Package{Name: "search", Version: "fa08207e"})
	return pkgSet
}

func createTestSrcs() []*testSrc {
	pkgs := make(map[string]struct{})
	pkgs["email"] = struct{}{}
	pkgs["video"] = struct{}{}
	tSrc := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Millisecond * 3,
		pkgs:     pkgs}

	pkgs = make(map[string]struct{})
	pkgs["video"] = struct{}{}
	pkgs["search"] = struct{}{}
	tSrc2 := testSrc{UpdateReqs: make(map[string]int),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Millisecond * 5,
		pkgs:     pkgs}
	return []*testSrc{&tSrc, &tSrc2}
}

func verifyReqCount(t *testing.T, srcs []*testSrc, pkgs *PackageSet, runs int) {
	pkgChecks := make(map[Package]int)

	for _, pkg := range pkgs.Packages() {
		pkgChecks[*pkg] = 0

		for _, src := range srcs {
			pkgChecks[*pkg] += src.UpdateReqs[pkg.Name]
		}

		//actRuns := src.UpdateReqs[pkg.Name]
		if pkgChecks[*pkg] != runs {
			t.Errorf("Incorrect execution count, found %d, but expected %d for %s\n", pkgChecks[*pkg], runs, pkg.Name)
		}
	}

	for _, src := range srcs {
		if len(src.getReqs) != 0 {
			t.Errorf("Error, some pkgs were not requested!")
		}
	}
}

func verifyGetResults(t *testing.T, pkgSet *PackageSet,
	updates map[Package]*GetResult) {
	if len(updates) != len(pkgSet.Packages()) {
		t.Errorf("Expected %d updates, but found %d\n",
			len(pkgSet.Packages()), len(updates))
	}

	for _, p := range pkgSet.Packages() {
		r, ok := updates[*p]
		if !ok {
			t.Errorf("No result returned for package %q\n", p.Name)
		}

		if r.Err != nil {
			t.Errorf("Error finding update for package %q: %v",
				p.Name, r.Err)
		}

		if r.Orig.Name != p.Name || r.Orig.Version != p.Version {
			t.Errorf("Update result does not match original key, expected %q, but found %q", r.Orig.String(), p.String())
		}
	}
}
