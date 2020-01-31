// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package isatty

// IsTerminal returns whether the system is in a terminal.
func IsTerminal() bool {
	return isTerminal()
}
