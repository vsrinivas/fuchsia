// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package init contains the `pm init` command
package init

import (
	"fuchsia.googlesource.com/pm/build"
)

// Run initializes package metadata in the given package directory. A manifest
// is generated with a name matching the directory name. A content manifest is
// also created including all files found in the package directory.
func Run(cfg *build.Config) error {
	return build.Init(cfg)
}
