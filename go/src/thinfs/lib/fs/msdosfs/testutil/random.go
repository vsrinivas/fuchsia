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
