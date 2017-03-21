// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fake

import (
	"math/rand"
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/block/blocktest"
)

const (
	numBlocks = 5493
	devSize   = numBlocks * blockSize
)

func setUp(t *testing.T) ([]byte, *rand.Rand) {
	seed := time.Now().UTC().UnixNano()
	t.Log("Seed is", seed)
	r := rand.New(rand.NewSource(seed))

	buf := make([]byte, devSize)
	r.Read(buf)

	return buf, r
}

func TestReadAt(t *testing.T) {
	buf, r := setUp(t)

	dev := Device(make([]byte, devSize))
	copy(dev, buf)

	blocktest.ReadAt(t, dev, r, buf)
}

func TestWriteAt(t *testing.T) {
	buf, r := setUp(t)

	dev := Device(make([]byte, devSize))
	copy(dev, buf)

	blocktest.ReadAt(t, dev, r, buf)
}

func TestErrorPaths(t *testing.T) {
	dev := Device(make([]byte, devSize))

	blocktest.ErrorPaths(t, dev)
}
