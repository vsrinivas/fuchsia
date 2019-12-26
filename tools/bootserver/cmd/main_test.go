// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/bootserver/lib"
)

func TestPopulateReaders(t *testing.T) {
	bootloaderImage := bootserver.Image{
		Name: "bootloader",
		Args: []string{"--bootloader"},
	}
	vbmetaRImage := bootserver.Image{
		Name: "zedboot.vbmeta",
		Args: []string{"--vbmetar"},
	}
	zedbootImage := bootserver.Image{
		Name: "zedboot",
		Args: []string{"--zirconr"},
	}

	allImgs := []bootserver.Image{bootloaderImage, vbmetaRImage, zedbootImage}

	tests := []struct {
		name                 string
		existingImageIndexes []int
		expectErr            bool
	}{
		{"PopulateAllReaders", []int{0, 1, 2}, false},
		{"FileNotFound", []int{0}, true},
	}

	for _, test := range tests {
		testImgs := make([]bootserver.Image, len(allImgs))
		copy(testImgs, allImgs)
		tmpDir, err := ioutil.TempDir("", "test-data")
		if err != nil {
			t.Fatalf("failed to create temp dir: %v", err)
		}
		defer os.RemoveAll(tmpDir)
		for i := range testImgs {
			imgPath := filepath.Join(tmpDir, testImgs[i].Name)
			testImgs[i].Path = imgPath
		}

		for _, i := range test.existingImageIndexes {
			err := ioutil.WriteFile(testImgs[i].Path, []byte("data"), 0755)
			if err != nil {
				t.Fatalf("Failed to create tmp file: %s", err)
			}
		}

		closeFunc, err := populateReaders(testImgs)

		if test.expectErr && err == nil {
			t.Errorf("Test%v: Expected errors; no errors found", test.name)
		}

		if !test.expectErr && err != nil {
			t.Errorf("Test%v: Expected no errors; found error - %v", test.name, err)
		}

		if test.expectErr && err != nil {
			continue
		}
		for _, img := range testImgs {
			if img.Reader == nil {
				t.Errorf("Test%v: missing reader for %s", test.name, img.Name)
			}
			// The contents of each image is `data` so the size should be 4.
			if img.Size != int64(4) {
				t.Errorf("Test%v: incorrect size for %s; actual: %d, expected: 4", test.name, img.Name, img.Size)
			}
			buf := make([]byte, 1)
			if _, err := img.Reader.ReadAt(buf, 0); err != nil {
				t.Errorf("Test%v: failed to read %s: %v", test.name, img.Name, err)
			}
		}
		closeFunc()
		for _, img := range testImgs {
			buf := make([]byte, 1)
			if _, err := img.Reader.ReadAt(buf, 0); err == nil || err == io.EOF {
				t.Fatalf("Test%v: reader is not closed for %s", test.name, img.Name)
			}
		}
	}
}
