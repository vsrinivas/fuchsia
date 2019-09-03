// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"fmt"
)

// Mode is a mode in which the testsharder can be run.
type Mode int

const (
	// Normal is the default mode in which all tests are sharded, except
	// those excluded by differing tags.
	Normal Mode = iota

	// Restricted is the mode in which auth-needing tests (i.e., those that
	// specify service accounts) are ignored. This mode is useful for
	// running untrusted code.
	Restricted
)

// String implements flag.Var.String.
func (m *Mode) String() string {
	switch *m {
	case Normal:
		return "normal"
	case Restricted:
		return "restricted"
	}
	return ""
}

// Set implements flag.Var.Set.
func (m *Mode) Set(s string) error {
	switch s {
	case "normal":
		*m = Normal
	case "restricted":
		*m = Restricted
	default:
		return fmt.Errorf("%s is not a valid mode", s)
	}
	return nil
}
