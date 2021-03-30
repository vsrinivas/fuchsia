// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testdriver

import (
	"path/filepath"
	"testing"
)

func TestNewManifest(t *testing.T) {
	manifestPath := filepath.Join(*testDataDir, "manifest", "single_prebuilt_test.json")
	m, err := NewManifest(manifestPath)
	if err != nil {
		t.Fatal(err)
	}

	if len(m.Tests) != 1 {
		t.Errorf("got %v; want %v\n", len(m.Tests), 1)
	}
}
