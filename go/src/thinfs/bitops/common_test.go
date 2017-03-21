// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bitops

import (
	"math/rand"
	"testing"
	"time"
)

func setUp(t *testing.T) *rand.Rand {
	seed := time.Now().UTC().UnixNano()
	t.Log("Seed is ", seed)
	return rand.New(rand.NewSource(seed))
}
