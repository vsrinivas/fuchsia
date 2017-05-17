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
	UpdateReqs map[string][]time.Time
	getReqs    map[Package]*struct{}
	interval   time.Duration
}

func (t *testSrc) AvailableUpdates(pkgs []*Package) (map[Package]Package, error) {
	t.mu.Lock()
	now := time.Now()

	updates := make(map[Package]Package)
	for _, p := range pkgs {
		tList := t.UpdateReqs[p.Name]
		if tList == nil {
			tList = []time.Time{}
		}
		tList = append(tList, now)
		t.UpdateReqs[p.Name] = tList
		up := Package{Name: p.Name, Version: randSeq(6)}
		t.getReqs[up] = &struct{}{}
		updates[*p] = up
	}

	t.mu.Unlock()
	fmt.Print("*")
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

	fmt.Print("|")
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

// TestDaemon tests daemon.go with a fake package source. The test runs for ~30
// seconds.
func TestDaemon(t *testing.T) {
	tSrc := testSrc{UpdateReqs: make(map[string][]time.Time),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Second * 3}
	tSrc2 := testSrc{UpdateReqs: make(map[string][]time.Time),
		getReqs:  make(map[Package]*struct{}),
		interval: time.Second * 5}
	sources := []*testSrc{&tSrc, &tSrc2}
	pkgSet := NewPackageSet()
	pkgSet.Add(&Package{Name: "email", Version: "23af90ee"})
	pkgSet.Add(&Package{Name: "video", Version: "f2b8006c"})
	pkgSet.Add(&Package{Name: "search", Version: "fa08207e"})

	d := NewDaemon(pkgSet, processPackage)

	startTime := time.Now()
	for _, src := range sources {
		d.AddSource(src)
	}

	// sleep for 30 seconds while we run
	runTime := 30 * time.Second
	time.Sleep(runTime)
	d.CancelAll()

	for _, src := range sources {
		verify(t, src, pkgSet, runTime, startTime)
	}
}

func verify(t *testing.T, src *testSrc, pkgs *PackageSet, runTime time.Duration,
	startTime time.Time) {
	runs := int(runTime/src.interval) + 1
	runsAlt := runs
	if runTime%src.interval == 0 {
		runsAlt--
	} else {
		runsAlt = -1
	}

	for _, pkg := range pkgs.Packages() {
		actRuns := src.UpdateReqs[pkg.Name]
		if len(actRuns) != runs && len(actRuns) != runsAlt {
			t.Errorf("Incorrect execution could, found %d, but expected %d for %s\n", len(actRuns), runs, pkg.Name)
		}

		thresh := src.interval / 10
		maxThresh := 10 * time.Second
		minThresh := 200 * time.Millisecond
		if thresh > maxThresh {
			thresh = maxThresh
		} else if thresh < minThresh {
			thresh = minThresh
		}

		for _, time := range actRuns {
			delta := time.Sub(startTime)
			expected := delta % src.interval
			if expected > thresh && expected <= src.interval-thresh {
				t.Errorf("Execution accuracy %s exceeds tolerance of %s\n", delta, thresh)
			}
		}
	}

	if len(src.getReqs) != 0 {
		t.Errorf("Error, some pkgs were not requested!")
	}
}
