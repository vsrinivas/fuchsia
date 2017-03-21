// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testutil

import (
	"math/rand"
	"time"

	"github.com/golang/glog"
)

// MakeRandomBuffer creates a random buffer of a specified size
func MakeRandomBuffer(size int) []byte {
	seed := time.Now().UnixNano()
	glog.V(2).Infof("Creating a new random buffer from seed: %d", seed)
	r := rand.New(rand.NewSource(seed))
	buf := make([]byte, size)
	for i := range buf {
		buf[i] = byte(r.Intn(100))
	}
	return buf
}
