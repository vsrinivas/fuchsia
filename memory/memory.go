// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package memory

// Total returns the total amount of memory on the system.
func Total() uint64 {
	return total()
}
