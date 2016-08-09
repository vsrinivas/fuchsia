// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package block

import (
	"math/rand"
	"testing"
	"time"

	"interfaces/block"
	"mojo/public/go/bindings"

	"fuchsia.googlesource.com/thinfs/lib/block/blocktest"
	"fuchsia.googlesource.com/thinfs/lib/block/fake"
	blk "fuchsia.googlesource.com/thinfs/mojo/services/block"
)

const (
	blockSize = 1024
	size      = 119 * blockSize
)

func setUp(t *testing.T) (*Device, *rand.Rand, []byte) {
	seed := time.Now().UTC().UnixNano()
	t.Log("seed is", seed)
	r := rand.New(rand.NewSource(seed))

	req, ptr := block.CreateMessagePipeForDevice()

	data := make([]byte, size)
	buf := make([]byte, size)
	r.Read(data)
	copy(buf, data)

	dev := blk.New(fake.Device(data), block.Capabilities_ReadWrite)
	stub := block.NewDeviceStub(req, dev, bindings.GetAsyncWaiter())
	go func() {
		for {
			if err := stub.ServeRequest(); err != nil {
				connErr, ok := err.(*bindings.ConnectionError)
				if !ok || !connErr.Closed() {
					t.Error(err)
				}
				break
			}
		}
	}()

	client, err := New(ptr)
	if err != nil {
		t.Fatal(err)
	}

	return client, r, buf
}

func TestReadAt(t *testing.T) {
	dev, r, buf := setUp(t)
	defer dev.Close()

	blocktest.ReadAt(t, dev, r, buf)
}

func TestWriteAt(t *testing.T) {
	dev, r, buf := setUp(t)
	defer dev.Close()

	blocktest.WriteAt(t, dev, r, buf)
}

func TestErrorPaths(t *testing.T) {
	dev, _, _ := setUp(t)
	defer dev.Close()

	blocktest.ErrorPaths(t, dev)
}
