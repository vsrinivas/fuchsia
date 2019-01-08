// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

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
