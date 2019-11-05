// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"fmt"
)

// Mode determines the arguments to use when booting/paving images.
type Mode int

const (
	// ModeNull is a null default that can be error checked against.
	ModeNull Mode = iota
	// ModePave is a directive to pave when booting.
	ModePave
	// ModeNetboot is a directive to netboot when booting.
	ModeNetboot
	// ModePaveZedboot is a directive to pave zedboot to partition A (the initial boot partition) when booting.
	ModePaveZedboot
)

// String returns the string value of the Mode type.
func (m *Mode) String() string {
	switch *m {
	case ModePave:
		return "pave"
	case ModeNetboot:
		return "netboot"
	case ModePaveZedboot:
		return "pave-zedboot"
	}
	return ""
}

// Set assigns a value to the Mode given a string
func (m *Mode) Set(s string) error {
	switch s {
	case "pave":
		*m = ModePave
	case "netboot":
		*m = ModeNetboot
	case "pave-zedboot":
		*m = ModePaveZedboot
	default:
		return fmt.Errorf("%s is not a valid mode", s)
	}
	return nil
}
