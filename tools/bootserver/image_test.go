// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"context"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func assertEqual(t *testing.T, actual, expected []string) {
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected output:\nactual:%v\nexpected:%v\n", actual, expected)
	}
}

func TestGetImages(t *testing.T) {
	var mockManifest = `[
  {
    "bootserver_pave": [
      "--bootloader"
    ],
    "name": "bootloader",
    "path": "bootloader",
    "type": "blk"
  },
  {
    "bootserver_pave": [
      "--zirconr"
    ],
    "name": "zircon-r",
    "path": "zedboot.zbi",
    "type": "zbi"
  },
  {
    "bootserver_netboot": [
      "--boot"
    ],
    "name": "netboot",
    "path": "netboot.zbi",
    "type": "zbi"
  },
  {
    "name": "non-existent",
    "path": "non-existent.zbi",
    "type": "zbi"
  }
]`

	tmpDir, err := ioutil.TempDir("", "test-data")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpDir)
	imgManifest := filepath.Join(tmpDir, "images.json")
	if err := ioutil.WriteFile(imgManifest, []byte(mockManifest), 0755); err != nil {
		t.Fatalf("failed to write image manifest: %v", err)
	}
	type testImage struct {
		name     string
		args     []string
		path     string
		contents string
		bootMode Mode
	}
	bootloaderImage := testImage{
		name:     "blk_bootloader",
		args:     []string{"--bootloader"},
		path:     filepath.Join(tmpDir, "bootloader"),
		contents: "bootloader contents",
		bootMode: ModePave,
	}
	zedbootImage := testImage{
		name:     "zbi_zircon-r",
		args:     []string{"--zirconr"},
		path:     filepath.Join(tmpDir, "zedboot.zbi"),
		contents: "zedboot contents",
		bootMode: ModePave,
	}
	netbootImage := testImage{
		name:     "zbi_netboot",
		args:     []string{"--boot"},
		path:     filepath.Join(tmpDir, "netboot.zbi"),
		contents: "netboot contents",
		bootMode: ModeNetboot,
	}
	allImages := make(map[string]testImage)
	for _, img := range []testImage{bootloaderImage, zedbootImage, netbootImage} {
		if err := ioutil.WriteFile(img.path, []byte(img.contents), 0755); err != nil {
			t.Fatalf("failed to write %s: %v", img.path, err)
		}
		allImages[img.name] = img
	}
	tests := []struct {
		name     string
		bootMode Mode
	}{
		{"NetbootImgs", ModeNetboot},
		{"UnknownBootMode", -1},
	}
	if err := os.Chdir(tmpDir); err != nil {
		t.Fatalf("failed to change the current directory: %v", err)
	}
	for _, test := range tests {
		imgs, closeFunc, err := GetImages(context.Background(), imgManifest, test.bootMode)
		if err != nil {
			t.Fatalf("Test%s: failed to get images: %v", test.name, err)
		}
		// The images returned should be all existing images.
		if len(imgs) != 3 {
			t.Errorf("Test%s: got %d images, expected 3", test.name, len(imgs))
		}
		for _, img := range imgs {
			expectedImg := allImages[img.Name]
			if expectedImg.bootMode == test.bootMode {
				assertEqual(t, img.Args, expectedImg.args)
			} else {
				assertEqual(t, img.Args, nil)
			}
			buf := make([]byte, img.Size)
			_, err := img.Reader.ReadAt(buf, 0)
			if err != nil {
				t.Fatalf("Test%s: failed to read img %s: %v", test.name, img.Path, err)
			}
			if string(buf) != expectedImg.contents {
				t.Fatalf("Test%s: unexpected image contents: expected: %s, actual: %v", test.name, expectedImg.contents, buf)
			}
		}
		closeFunc()
		for _, img := range imgs {
			buf := make([]byte, 1)
			if _, err := img.Reader.ReadAt(buf, 0); err == nil || err == io.EOF {
				t.Fatalf("Test%s: reader is not closed for %s", test.name, img.Name)
			}
		}
	}
}
