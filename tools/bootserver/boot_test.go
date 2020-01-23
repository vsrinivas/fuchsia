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
	numImages := 4
	for i := 0; i < numImages; i++ {
		imgs = append(imgs, Image{
			Name:   fmt.Sprintf("image%d", i),
			Reader: bytes.NewReader([]byte(fmt.Sprintf("content of image%d", i))),
			Args:   []string{"--arg"},
		})
	}
	// Add another image without Args. This image should not be downloaded.
	imgs = append(imgs, Image{
		Name:   "noArgsImage",
		Reader: bytes.NewReader([]byte("content of noArgsImage")),
	})
	newImgs, closeFunc, err := downloadImagesToDir(tmpDir, imgs)
	if err != nil {
		t.Fatalf("failed to download image: %v", err)
	}
	defer closeFunc()
	if len(newImgs) != numImages {
		t.Errorf("unexpected number of images downloaded; expected: %d, actual: %d", numImages, len(newImgs))
	}
	for _, img := range newImgs {
		if img.Name == "noArgsImage" {
			t.Errorf("downloaded an image with no args")
		}
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
