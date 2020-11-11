// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package util

import "gen/config"

var fidlFiles []config.FidlFile

func Register(ff config.FidlFile) {
	fidlFiles = append(fidlFiles, ff)
}

func AllFidlFiles() []config.FidlFile {
	return fidlFiles
}
