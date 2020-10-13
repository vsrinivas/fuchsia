// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestOrderedBlobInfo(t *testing.T) {
	cfg := TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	BuildTestPackage(cfg)

	blobs, err := cfg.BlobInfo()
	if err != nil {
		t.Fatal(err)
	}

	// The meta far is always first, and all following content entries are
	// sorted by the path in the package.
	expectedPaths := []string{"meta/", "a", "b", "dir/c", "rand1", "rand2"}

	actualPaths := make([]string, 0, len(blobs))
	for _, blob := range blobs {
		actualPaths = append(actualPaths, blob.Path)
	}

	if !reflect.DeepEqual(actualPaths, expectedPaths) {
		t.Errorf("got %v, want %v", actualPaths, expectedPaths)
	}
}
