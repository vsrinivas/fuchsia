// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"bytes"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestDownloadImage(t *testing.T) {
	tmpDir, err := ioutil.TempDir("", "test-data")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)
	expectedData := "content for test image to download"
	reader := bytes.NewReader([]byte(expectedData))
	imgPath := filepath.Join(tmpDir, "image")
	f, err := downloadAndOpenImage(imgPath, Image{
		Name:   "image",
		Reader: reader,
	})
	if err != nil {
		t.Fatalf("failed to download image: %v", err)
	}
	f.Close()
	content, err := ioutil.ReadFile(imgPath)
	if err != nil {
		t.Fatalf("failed to read file: %v", err)
	}
	if string(content) != expectedData {
		t.Fatalf("unexpected content: expected: %s, actual: %s", expectedData, content)
	}
}
