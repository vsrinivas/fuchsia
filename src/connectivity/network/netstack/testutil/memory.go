// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testutil

import (
	"fmt"
	"runtime"
)

// CheckHeapObjectsGrowth runs the given function f for the given runCount times
// and checks whether the growth is under acceptableGrowth.
//
// It is similar to testing.AllocsPerRun, but it checks the number of newly
// allocated objects that are still referenced after the runs, rather than the
// the number of allocations performed.
//
// Note that the runtime may allocate/free heap objects unrelated to the
// function being checked. The acceptableGrowth should take this into account.
func CheckHeapObjectsGrowth(runCount int, acceptableGrowth uint64, f func()) (err error) {
	// Only allow a single userspace goroutine to run concurrently, in an attempt
	// to minimize noise in allocations. Depending on f, it may make the runs
	// slower but results are more consistent.
	const tmpGoMaxProcs = 1
	restoreGoMaxProcs := runtime.GOMAXPROCS(tmpGoMaxProcs)
	defer func() {
		if got, want := runtime.GOMAXPROCS(restoreGoMaxProcs), tmpGoMaxProcs; got != want {
			// Overwrite the return value. This error takes priority, because an
			// invalid result may be caused by GOMAXPROCS not correctly set to 1.
			err = fmt.Errorf("unexpected GOMAXPROCS() value: got = %d, want = %d", got, want)
		}
	}()

	// Warm up the function, in case there are extra allocations made on the first
	// run only.
	f()

	var memStats runtime.MemStats
	runtime.GC()
	runtime.ReadMemStats(&memStats)
	before := memStats.HeapObjects
	for i := 0; i < runCount; i++ {
		f()
	}
	runtime.GC()
	runtime.ReadMemStats(&memStats)
	after := memStats.HeapObjects
	if before > after {
		return nil
	}

	growth := after - before
	if growth > acceptableGrowth {
		return fmt.Errorf("got heap growth of %d objects, want <= %d", growth, acceptableGrowth)
	}

	return nil
}
