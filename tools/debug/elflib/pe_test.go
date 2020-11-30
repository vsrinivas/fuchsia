// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elflib

import (
	"encoding/hex"
	"os"
	"path/filepath"
	"testing"
)

func testPeBuildIDFile(t *testing.T, filename string, expected string) {
	testfile := filepath.Join(*testDataDir, filename)
	f, err := os.Open(testfile)
	if err != nil {
		t.Fatal("from os.Open: ", err)
	}
	defer f.Close()
	buildIDs, err := GetBuildIDs(testfile, f)
	if err != nil {
		t.Fatal("from PeGetBuildIDs: ", err)
	}
	if len(buildIDs) != 1 {
		t.Fatal("expected one build ID but got ", buildIDs)
	}
	actual := hex.EncodeToString(buildIDs[0])
	if actual != expected {
		t.Fatal("expected ", expected, " but got ", actual, buildIDs[0])
	}
	if hex.EncodeToString(buildIDs[0]) != expected {
		t.Fatal("expected ", expected, " but got ", buildIDs[0])
	}
}

func TestPeBuildIDs(t *testing.T) {
	testPeBuildIDFile(t, "pe-x64.efi", "ab21b543f82d5cc94c4c44205044422e00000001")
}

// TODO: Go's debug/pe doesn't handle arm64 binaries
func disabledTestArmPeBuildIDs(t *testing.T) {
	testPeBuildIDFile(t, "pe-aa64.efi", "def")
}
