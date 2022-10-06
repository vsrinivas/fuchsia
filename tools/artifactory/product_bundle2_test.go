// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements productBundlesModules.
type mockProductBundlesModules struct {
	productBundles []build.ProductBundle
	buildDir       string
}

func (m mockProductBundlesModules) BuildDir() string {
	return m.buildDir
}

func (m mockProductBundlesModules) ProductBundles() []build.ProductBundle {
	return m.productBundles
}

func TestProductBundle2Uploads(t *testing.T) {
	uploadManifest := []byte(`{
    "version": "1",
    "entries": [
      {
        "source": "one",
        "destination": "system_a/one"
      },
      {
        "source": "two",
        "destination": "system_b/two"
      }
    ]
  }`)
	dir := t.TempDir()
	uploadManifestName := "upload.json"
	if err := os.WriteFile(filepath.Join(dir, uploadManifestName), uploadManifest, 0o600); err != nil {
		t.Fatalf("failed to write to fake upload.json file: %v", err)
	}

	m := &mockProductBundlesModules{
		productBundles: []build.ProductBundle{
			{
				Path:               dir,
				UploadManifestPath: uploadManifestName,
			},
		},
		buildDir: dir,
	}

	expected := []Upload{
		{
			Source:      filepath.Join(dir, "one"),
			Destination: filepath.Join("namespace", "system_a", "one"),
		},
		{
			Source:      filepath.Join(dir, "two"),
			Destination: filepath.Join("namespace", "system_b", "two"),
		},
	}

	actual, err := productBundle2Uploads(m, "namespace")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected product bundle uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}
