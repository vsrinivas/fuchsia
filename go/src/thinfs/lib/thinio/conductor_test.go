// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package thinio

import (
	"bytes"
	"math/rand"
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/block/fake"
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
