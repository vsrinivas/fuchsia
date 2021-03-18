// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testdriver

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	cipd "go.fuchsia.dev/fuchsia/sdk/cts/tools/testdriver/cipd"
)

const (
	cipdPkg = "fuchsia/sdk/gn/linux-amd64"
)

// Verify that new SDK object is created successfully.
// Verify that the target SDK and work directories are created successfully.
func TestNewSDK(t *testing.T) {
	sdkVersion := "0.20210111.2.1"
	dir, err := ioutil.TempDir("", "sdkdir")
	if err != nil {
		t.Fatal(err)
	}

	_, err = NewSDK(cipdPkg, sdkVersion, dir)
	if err != nil {
		t.Fatal(err)
	}
}

// Test the SDK download functionality.
// Use the CIPD stub to prevent actually accessing CIPD during the test.
func TestDownloadSDK(t *testing.T) {
	dir, err := ioutil.TempDir("", "sdkdir")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(dir)

	sdk, err := NewSDK(cipdPkg, "0.20210111.2.1", dir)
	if err != nil {
		t.Error(err)
	}

	cipdStub := cipd.NewStub()
	err = sdk.Download(cipdStub)
	if err != nil {
		t.Error(err)
	}

	sdkDir := filepath.Join(dir, "sdk")
	if _, err := os.Stat(sdkDir); os.IsNotExist(err) {
		t.Errorf("Expected sdk directory \"%v\" to be created, but it wasn't.\n", sdkDir)
	}
}
