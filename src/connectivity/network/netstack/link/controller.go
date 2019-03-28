// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package link

import (
	"fmt"
)

type State int

const (
	StateUnknown State = iota
	StateStarted
	StateDown
	StateClosed
)

type Controller interface {
	Up() error
	Down() error
	Close() error
	SetOnStateChange(func(State))

	// TODO(stijlist): remove all callers of this method;
	// not all interfaces are backed by a topological path
	// (e.g. loopback, bridge).
	Path() string
	SetPromiscuousMode(bool) error
}

func (s State) String() string {
	switch s {
	case StateUnknown:
		return "link unknown state"
	case StateStarted:
		return "link started"
	case StateDown:
		return "link down"
	case StateClosed:
		return "link stopped"
	default:
		return fmt.Sprintf("link bad state (%d)", s)
	}
}
