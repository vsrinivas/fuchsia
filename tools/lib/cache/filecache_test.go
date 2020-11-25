// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package cache

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"io/ioutil"
	"math/rand"
	"runtime"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

// hammerThread is a way for users to configure specific loads on the cache.
// the times are not maintained precisely due to the fact that the time it
// takes to perform an operation on the cache is assumed to be zero.
type hammerThread struct {
	// ReadsPerSec sets the mean rate of read operations for this thread in
	// reads per second
	ReadsPerSec float64 `json:"rps"`
	// ReadTime sets the mean time to hold on to a file for before closing.
	ReadTime float64 `json:"read_time"`
	// WritesPerSec sets the mean rate of write operations for this thread in
	// reads per second.
	WritesPerSec float64 `json:"wps"`
	// Duration is the number of seconds this thread should run for.
	Duration float64 `json:"duration"`
}

type hammerProc struct {
	Threads []hammerThread `json:"threads"`
}

type fuzzerProc struct {
	hammerProc
	cache *FileCache
}

type fuzzKey string

func (f fuzzKey) Hash() string {
	return string(f)
}

type cacheFuzzer struct {
	cachePath string
	size      uint64
	procs     []fuzzerProc
	// TODO: Figure out if write contention is making things slow and bucket
	// the keys so that a lock only has to be obtained for the particular part
	// of the map being used.
	recordLock sync.RWMutex
	record     map[string]string
	// These need to be updated atomically.
	totalMisses uint64
	totalTries  uint64
	totalWrites uint64
	writeFails  uint64
	// Makes sure we wait on all threads.
	wg sync.WaitGroup
}

func newCacheFuzzer(config string, cachePath string, size uint64) (*cacheFuzzer, error) {
	file := bytes.NewBufferString(config)
	var out cacheFuzzer
	out.cachePath = cachePath
	out.size = size
	dec := json.NewDecoder(file)
	var procs []hammerProc
	dec.Decode(&procs)
	for _, proc := range procs {
		filecache, err := GetFileCache(cachePath, size)
		if err != nil {
			return nil, err
		}
		out.procs = append(out.procs, fuzzerProc{proc, filecache})
	}
	out.record = make(map[string]string)
	return &out, nil
}

func (c *cacheFuzzer) checkRead(t *testing.T, readTime float64, filecache *FileCache) {
	// Acquire the lock for the minimum amount of time. We want the actual cache
	// read to not be blocked by the lock.
	var key string
	var value string
	foundSample := func() bool {
		c.recordLock.RLock()
		defer func() {
			c.recordLock.RUnlock()
			runtime.Gosched()
		}()
		for k, v := range c.record {
			key, value = k, v
			return true
		}
		return false
	}()
	// Make sure we actually sampled a real key/value pair
	if !foundSample {
		return
	}
	// Now test that the key and value make sense.
	atomic.AddUint64(&c.totalTries, 1)
	ref, err := filecache.Get(fuzzKey(key))
	// Note that if we miss, it was because the file was culled. We don't attempt
	// to test that keys that were never added hit this case.
	if err != nil {
		atomic.AddUint64(&c.totalMisses, 1)
		return
	}
	// Make sure we wait on this to close
	c.wg.Add(1)
	// Now hold on to the file for a random amount of time.
	go func() {
		defer c.wg.Done()
		defer ref.Close()
		sleepRand(readTime)
		data, err := ioutil.ReadFile(ref.String())
		if err != nil {
			t.Errorf("could not read file: %v", err)
		}
		if string(data) != value {
			t.Errorf("could not read file: %v", err)
		}
	}()
}

// Generates a random hex string of 16-bytes with maximal entropy.
// This should give enough enough entropy to ensure that the same
// string is never used twice.
func getRandomString() string {
	out := make([]byte, 16)
	rand.Read(out)
	return hex.EncodeToString(out)
}

// This function shouldn't actually fail but adds new things to the cache
// so that further gets can be tested later.
func (c *cacheFuzzer) checkWrite(t *testing.T, filecache *FileCache) {
	// We want to perform the write without a lock. The only part that should
	// lock is updating the internal data structure we use to keep track of keys.
	key := getRandomString()
	value := getRandomString()
	atomic.AddUint64(&c.totalWrites, 1)
	file, err := filecache.Add(fuzzKey(key), bytes.NewBufferString(value))
	if err != nil {
		t.Errorf("%v", err)
		atomic.AddUint64(&c.writeFails, 1)
		return
	}
	file.Close()
	c.recordLock.Lock()
	defer c.recordLock.Unlock()
	c.record[key] = value
}

// Sleep for a number of random number of seconds with mean 'beta'.
// Entropy is maximized. To get an expected number of operations per
// second of N you can use 1/N as beta to get maximally entropicly timed
// events. The amount of time slept for is returned.
func sleepRand(beta float64) float64 {
	out := rand.ExpFloat64() * beta
	time.Sleep(time.Duration(out * float64(time.Second)))
	return out
}

func (c *cacheFuzzer) fuzzThread(t *testing.T, proc fuzzerProc, thread hammerThread) {
	// lambda is the expected number of operations per second
	lambda := thread.ReadsPerSec + thread.WritesPerSec
	// readProb is the probability that one of our operations will be a read.
	readProb := thread.ReadsPerSec / lambda
	// we need to keep track of the total time we've slept for and die at the end.
	totalTime := 0.0
	for totalTime < thread.Duration {
		totalTime += sleepRand(1.0 / lambda)
		if rand.Float64() < readProb {
			c.checkRead(t, thread.ReadTime, proc.cache)
		} else {
			c.checkWrite(t, proc.cache)
		}
	}
	// Note that checkRead can call c.wg.Add. It is safe for it to call Add as
	// long as it proceeds this Done.
	c.wg.Done()
}

func (c *cacheFuzzer) fuzzProc(t *testing.T, proc fuzzerProc) {
	for _, thread := range proc.Threads {
		c.wg.Add(1)
		go c.fuzzThread(t, proc, thread)
	}
}

func (c *cacheFuzzer) fuzz(t *testing.T) {
	for _, proc := range c.procs {
		c.fuzzProc(t, proc)
	}
	c.wg.Wait()
}

const fuzzConfig = `
[
  {"threads":[
    { "rps": 1000.0, "read_time": 0.000001, "wps": 100.0, "duration": 1.0 },
    { "rps": 2000.0, "read_time": 0.15, "wps": 100.0, "duration": 0.5 },
    { "rps": 10.0, "read_time": 0.001, "wps": 1000.0, "duration": 1.1 },
    { "rps": 5000.0, "read_time": 0.1, "wps": 10.0, "duration": 0.7 }
  ]},
  {"threads":[
    { "rps": 1000.0, "read_time": 0.01, "wps": 100.0, "duration": 1.1 },
    { "rps": 2000.0, "read_time": 0.0001, "wps": 100.0, "duration": 1.2 },
    { "rps": 10.0, "read_time": 0.05, "wps": 1000.0, "duration": 0.3 },
    { "rps": 5000.0, "read_time": 0.001, "wps": 10.0, "duration": 1.3 }
  ]},
  {"threads":[
    { "rps": 1000.0, "read_time": 0.01, "wps": 100.0, "duration": 0.5 },
    { "rps": 2000.0, "read_time": 0.001, "wps": 100.0, "duration": 1.0 },
    { "rps": 10.0, "read_time": 0.05, "wps": 1000.0, "duration": 1.5 },
    { "rps": 5000.0, "read_time": 0.005, "wps": 10.0, "duration": 1.0 }
  ]}
]
`

func TestFileCache(t *testing.T) {
	cachePath := t.TempDir()
	filecache, err := newCacheFuzzer(fuzzConfig, cachePath, 250)
	if err != nil {
		t.Fatalf("%v", err)
	}
	filecache.fuzz(t)
	t.Logf("cache hit/miss ratio: %v:%v", filecache.totalTries-filecache.totalMisses, filecache.totalMisses)
	t.Logf("write success/fail ratio: %v:%v", filecache.totalWrites-filecache.writeFails, filecache.writeFails)
	if err := DeleteCache(cachePath); err != nil {
		t.Error(err)
	}
}
