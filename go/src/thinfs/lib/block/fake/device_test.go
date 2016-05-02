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

package fake

import (
	"math/rand"
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/block/blocktest"
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
