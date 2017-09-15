// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package thinio

import (
	"bytes"
	"math/rand"
	"testing"
	"time"

	"thinfs/block/fake"
)

const (
	cacheSize  = 256 * 1024       // 256K
	deviceSize = 32 * 1024 * 1024 // 32M

	numIterations = 4916

	maxWriteSize = 64 * 1024 // 64K
)

func setUp(t *testing.T) (*Conductor, []byte, *rand.Rand) {
	seed := time.Now().UTC().UnixNano()
	t.Log("Seed is ", seed)
	r := rand.New(rand.NewSource(seed))

	dev := make([]byte, deviceSize)
	r.Read(dev)

	c := NewConductor(fake.Device(dev), cacheSize)

	return c, dev, r
}

func TestSize(t *testing.T) {
	c, _, _ := setUp(t)
	size := c.DeviceSize()
	if size != deviceSize {
		t.Fatalf("Device size was seen as %d (expected %d)\n", size, deviceSize)
	}

	if err := c.Close(); err != nil {
		t.Fatal(err)
	}
}

func TestReadAt(t *testing.T) {
	c, dev, r := setUp(t)

	off := r.Int63n(deviceSize)
	p := make([]byte, r.Int63n(deviceSize-off))
	if _, err := c.ReadAt(p, off); err != nil {
		t.Fatal("Error reading data: ", err)
	}

	if !bytes.Equal(p, dev[off:off+int64(len(p))]) {
		t.Fatalf("c.ReadAt(p, %v) != dev[%v:%v]\n", off, off, off+int64(len(p)))
	}
}

func TestWriteAt(t *testing.T) {
	c, dev, r := setUp(t)

	off := r.Int63n(deviceSize)
	p := make([]byte, r.Int63n(deviceSize-off))
	r.Read(p)

	if _, err := c.WriteAt(p, off); err != nil {
		t.Fatal("Error writing data: ", err)
	}
	c.Flush()

	if !bytes.Equal(p, dev[off:off+int64(len(p))]) {
		t.Fatalf("c.WriteAt(p, %v) != dev[%v:%v]\n", off, off, off+int64(len(p)))
	}
}

func min(a, b int64) int64 {
	if a < b {
		return a
	}

	return b
}

func TestFuzz(t *testing.T) {
	c, dev, r := setUp(t)
	checker := make([]byte, deviceSize)
	copy(checker, dev)
	for i := 0; i < numIterations; i++ {
		off := r.Int63n(deviceSize)
		p := make([]byte, r.Int63n(min(maxWriteSize, deviceSize-off)))

		if r.Int()&0x1 == 0 {
			r.Read(p)
			copy(checker[off:], p)
			if _, err := c.WriteAt(p, off); err != nil {
				t.Errorf("Error writing %v bytes to offset %#x: %v\n", len(p), off, err)
			}
		} else {
			if _, err := c.ReadAt(p, off); err != nil {
				t.Errorf("Error reading %v bytes from offset %#x: %v\n", len(p), off, err)
			}
			if !bytes.Equal(p, checker[off:off+int64(len(p))]) {
				t.Errorf("%v byte slice at offset %#x differs from expected\n", len(p), off)
			}
		}
	}

	c.Flush()
	if !bytes.Equal(dev, checker) {
		t.Error("Final byte slices differ")
	}
}

// Don't even bother validating the device itself -- just ensure that the cache does not crash when
// accessed concurrently.
func TestConcurrencySimple(t *testing.T) {
	c, _, _ := setUp(t)

	writer := func(chanEnd chan bool) {
		seed := time.Now().UTC().UnixNano()
		r := rand.New(rand.NewSource(seed))

		for i := 0; i < numIterations; i++ {
			// Write random bytes to an offset.
			off := r.Int63n(deviceSize)
			p := make([]byte, r.Int63n(min(maxWriteSize, deviceSize-off)))
			r.Read(p)
			if _, err := c.WriteAt(p, off); err != nil {
				t.Errorf("Error writing %v bytes to offset %#x: %v\n", len(p), off, err)
			}
		}
		chanEnd <- true
	}

	reader := func(chanEnd chan bool) {
		seed := time.Now().UTC().UnixNano()
		r := rand.New(rand.NewSource(seed))

		for i := 0; i < numIterations; i++ {
			// Read random bytes from an offset (don't bother verifying the read).
			off := r.Int63n(deviceSize)
			p := make([]byte, r.Int63n(min(maxWriteSize, deviceSize-off)))

			if _, err := c.ReadAt(p, off); err != nil {
				t.Errorf("Error reading %v bytes from offset %#x: %v\n", len(p), off, err)
			}
		}
		chanEnd <- true
	}

	chanEnd := make(chan bool)
	numWriters := 3
	numReaders := 3

	for i := 0; i < numWriters; i++ {
		go writer(chanEnd)
	}
	for i := 0; i < numReaders; i++ {
		go reader(chanEnd)
	}

	// Wait for everyone to finish
	for i := 0; i < numWriters+numReaders; i++ {
		<-chanEnd
	}

	if err := c.Flush(); err != nil {
		t.Fatal(err)
	}
}

// Serialize all writes, but use multiple concurrent readers.
func TestConcurrencyOneWriterMultipleReaders(t *testing.T) {
	c, dev, _ := setUp(t)
	checker := make([]byte, deviceSize)
	copy(checker, dev)
	writer := func(chanEnd chan bool) {
		seed := time.Now().UTC().UnixNano()
		r := rand.New(rand.NewSource(seed))

		for i := 0; i < numIterations; i++ {
			// Write random bytes to an offset.
			off := r.Int63n(deviceSize)
			p := make([]byte, r.Int63n(min(maxWriteSize, deviceSize-off)))
			r.Read(p)
			copy(checker[off:], p)
			if _, err := c.WriteAt(p, off); err != nil {
				t.Errorf("Error writing %v bytes to offset %#x: %v\n", len(p), off, err)
			}
		}
		chanEnd <- true
	}

	reader := func(chanEnd chan bool) {
		seed := time.Now().UTC().UnixNano()
		r := rand.New(rand.NewSource(seed))

		for i := 0; i < numIterations; i++ {
			// Read random bytes from an offset (don't bother verifying the read).
			off := r.Int63n(deviceSize)
			p := make([]byte, r.Int63n(min(maxWriteSize, deviceSize-off)))

			if _, err := c.ReadAt(p, off); err != nil {
				t.Errorf("Error reading %v bytes from offset %#x: %v\n", len(p), off, err)
			}
		}
		chanEnd <- true
	}

	chanEnd := make(chan bool)
	numWriters := 1
	numReaders := 3

	for i := 0; i < numWriters; i++ {
		go writer(chanEnd)
	}
	for i := 0; i < numReaders; i++ {
		go reader(chanEnd)
	}

	// Wait for everyone to finish
	for i := 0; i < numWriters+numReaders; i++ {
		<-chanEnd
	}

	if err := c.Flush(); err != nil {
		t.Fatal(err)
	}

	// Validate the serialized writes were not altered by the reads in any way.
	if !bytes.Equal(dev, checker) {
		t.Error("Final byte slices differ")
	}
}
