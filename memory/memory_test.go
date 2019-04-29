// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package memory

import (
	"testing"
)

func TestNonZero(t *testing.T) {
	if Total() == 0 {
		t.Fatal("Total returned 0")
	}
}
