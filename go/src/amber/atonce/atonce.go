// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// atonce is essentially like the singleflight package from groupcache, but global
// and with a simpler interface.
package atonce

import (
	"sync"
)

type job struct {
	sync.WaitGroup
	err error
}

type jobkey struct {
	group string
	key   string
}

var (
	mu   sync.Mutex
	jobs = make(map[jobkey]*job)
)

// Do will perform f concurrently at most once for all overlapping calls with
// the same group & key. All concurrent callers receive the same result. Callers
// should be careful not to let external actors take full control over groups &
// keys, in order to avoid externally influenced, but invalid collisions.
// Typically a "group" is a string unique across the program representing a
// particular task that may occur concurrently, and key is a value unique to
// that particular job, e.g. "updateMetadata", "example.org"
func Do(group, key string, f func() error) error {
	mu.Lock()

	jk := jobkey{group, key}

	if j, found := jobs[jk]; found {
		mu.Unlock()
		j.Wait()
		return j.err
	}

	var j job
	j.Add(1)
	jobs[jk] = &j
	mu.Unlock()

	j.err = f()
	j.Done()

	mu.Lock()
	delete(jobs, jk)
	mu.Unlock()

	return j.err
}
