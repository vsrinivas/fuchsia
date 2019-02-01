// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package genkey

import (
	"os"
	"path/filepath"
	"testing"

	"fuchsia.googlesource.com/pm/build"
)

func TestRun(t *testing.T) {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))

	if err := Run(cfg, []string{}); err != nil {
		t.Fatal(err)
	}

	if _, err := os.Stat(cfg.KeyPath); err != nil {
		t.Errorf("genkey didn't write a key!")
	}
}
