// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"os"
	"path/filepath"
)

// Unpack the QEMU instance.
func Unpack() error {
	ex, err := os.Executable()
	if err != nil {
		return err
	}
	exPath := filepath.Dir(ex)
	archivePath := filepath.Join(exPath, "test_data/qemu/qemu.tar.gz")

	_, err = os.Stat(archivePath)
	return err
}
