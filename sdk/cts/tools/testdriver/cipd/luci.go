// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// LUCI implements the CIPD interface by using the CIPD go library.
// More info: https://pkg.go.dev/go.chromium.org/luci/cipd

// TODO(fxb/71515): Implement once the go library has been imported.

package testdriver

import (
	"fmt"
)

type LUCI struct {
}

func NewLUCI() *LUCI {
	return &LUCI{}
}

// Find the CIPD version (a very long string of characters) of a given package.
func (l *LUCI) GetVersion(pkg string, tags []*Tag, refs []*Ref) (PkgInstance, error) {
	return PkgInstance{}, fmt.Errorf("Not Implemented: fxb/71515")
}

// Download retrieves the specified package at the specified CIPD version, and
// installs it in the location specified by "dest" on the local filesystem.
func (c *LUCI) Download(pkg PkgInstance, dest string) error {
	return fmt.Errorf("Not Implemented: fxb/71515")
}
