// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"os"
	"sync"
	"testing"
	"time"
)

type testSrc struct {
	mu         sync.Mutex
	UpdateReqs map[string][]time.Time
	getReqs    map[string]*struct{}
}

func (t *testSrc) FetchUpdate(pkg *Package) (*Package, error) {
	t.mu.Lock()
	now := time.Now()
	tList := t.UpdateReqs[pkg.String()]
	if tList == nil {
		tList = []time.Time{}
	}
	tList = append(tList, now)
	t.UpdateReqs[pkg.String()] = tList
	p := Package{Name: pkg.Name, Version: randSeq(6)}
	t.getReqs[p.String()] = &struct{}{}
	t.mu.Unlock()

	fmt.Print(".")
	return &p, nil
}

func (t *testSrc) FetchPkg(pkg *Package) (*os.File, error) {
	t.mu.Lock()
	if t.getReqs[pkg.String()] == nil {
		fmt.Println("ERROR: unknown update pkg requested")
		return nil, ErrNoUpdateContent
	}

	delete(t.getReqs, pkg.String())
	t.mu.Unlock()

	fmt.Print("|")
	return nil, nil
}

func (t *testSrc) CheckInterval() time.Duration {
	return 1 * time.Millisecond
}

func (t *testSrc) Equals(o Source) bool {
	switch p := o.(type) {
	case *testSrc:
		return t == p
	default:
		return false
	}
}

// TestDaemon tests daemon.go with a fake package source. The test runs for ~30
// seconds.
func TestDaemon(t *testing.T) {
	srcSet := SourceSet{}
	tSrc := testSrc{UpdateReqs: make(map[string][]time.Time),
		getReqs: make(map[string]*struct{})}
	srcSet.AddSource(&tSrc)

	job1 := NewUpdateRequest(
		[]Package{
			Package{Name: "email", Version: "23af90ee"}},
		7*time.Second)

	job2 := NewUpdateRequest(
		[]Package{
			Package{Name: "video", Version: "f2b8006c"}},
		3*time.Second)

	job3 := NewUpdateRequest(
		[]Package{
			Package{Name: "search", Version: "fa08207e"}},
		1*time.Second)

	jobs := []*UpdateRequest{job1, job2, job3}
	d := NewDaemon(&srcSet)
	startTime := time.Now()
	for _, j := range jobs {
		d.AddRequest(j)
	}

	// sleep for 30 seconds while we run
	runTime := 30 * time.Second
	time.Sleep(runTime)
	d.CancelAll()

	for k, v := range tSrc.UpdateReqs {
		var targJob *UpdateRequest
	Outer:
		for i := range jobs {
			targets := jobs[i].GetTargets()
			for j := range targets {
				if targets[j].String() == k {
					targJob = jobs[i]
					break Outer
				}
			}
		}

		times := v
		// check that we did the number of checks expected
		expectedRuns := int(runTime/targJob.UpdateInterval) + 1
		if expectedRuns != len(times) {
			// special case when interval is a multiple of job time,
			// here we expect one less run perhaps than regular
			if runTime%targJob.UpdateInterval == 0 {
				if expectedRuns-1 != len(times) {
					t.Errorf("Incorrect execution count, found %d but expected %d\n",
						len(times), expectedRuns-1)
				}
			} else {
				t.Errorf("Wrong execution count, found %d but expected %d\n",
					len(times), expectedRuns)
			}
		}

		// check that the trains ran on time

		// set a threshold of being within 10%, but we should always be
		// within 10 seconds and don't require we be closer than 200ms
		// of our interval target
		thresh := targJob.UpdateInterval / 10
		maxThresh := 10 * time.Second
		minThresh := 200 * time.Millisecond
		if thresh > maxThresh {
			thresh = maxThresh
		} else if thresh < minThresh {
			thresh = minThresh
		}

		for _, time := range times {
			delta := time.Sub(startTime)
			expected := delta % targJob.UpdateInterval
			if expected > thresh && expected <= targJob.UpdateInterval-thresh {
				t.Errorf("Execution accuracy %s exceeds tolerance of %s\n", delta, thresh)
			}
		}
	}

	if len(tSrc.getReqs) != 0 {
		t.Errorf("Error, some pkgs were not requested!")
	}
}
