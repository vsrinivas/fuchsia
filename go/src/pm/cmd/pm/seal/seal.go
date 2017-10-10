// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package seal implements the `pm seal` command
package seal

import (
	"fuchsia.googlesource.com/pm/build"
)

// Run first delegates to sign.Run to generate a fresh signature for the
// package in packageDir, then archives the meta/ directory into meta.far.
func Run(cfg *build.Config) error {
	_, err := build.Seal(cfg)
	return err
}
