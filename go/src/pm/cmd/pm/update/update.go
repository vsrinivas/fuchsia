// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package update contains the `pm update` command
package update

import (
	"fuchsia.googlesource.com/pm/build"
)

// Run executes the `pm update` command
func Run(cfg *build.Config) error {
	return build.Update(cfg)
}
