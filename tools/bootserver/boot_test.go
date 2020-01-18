// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestDownloadImagesToDir(t *testing.T) {
	tmpDir, err := ioutil.TempDir("", "test-data")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)
	var imgs []Image
	for i := 0; i < 4; i++ {
		imgs = append(imgs, Image{
			Name:   fmt.Sprintf("image%d", i),
			Reader: bytes.NewReader([]byte(fmt.Sprintf("content of image%d", i))),
		})
	}
	newImgs, closeFunc, err := downloadImagesToDir(tmpDir, imgs)
	if err != nil {
		t.Fatalf("failed to download image: %v", err)
	}
	defer closeFunc()
	for _, img := range newImgs {
		content, err := ioutil.ReadFile(filepath.Join(tmpDir, img.Name))
		if err != nil {
			t.Fatalf("failed to read file: %v", err)
		}
		expectedData := fmt.Sprintf("content of %s", img.Name)
		if string(content) != expectedData {
			t.Errorf("unexpected content: expected: %s, actual: %s", expectedData, content)
		}
		if int(img.Size) != len(content) {
			t.Errorf("incorrect size: expected: %d, actual: %d", img.Size, len(content))
		}
	}
}
