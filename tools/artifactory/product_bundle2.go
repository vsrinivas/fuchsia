// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"fmt"
	"os"
	"path"

	"go.fuchsia.dev/fuchsia/tools/build"
)

type ProductBundleUploadManifest struct {
	Version string   `json:"version"`
	Entries []Upload `json:"entries"`
}

type productBundlesModules interface {
	BuildDir() string
	ProductBundles() []build.ProductBundle
}

// ProductBundle2Uploads parses the product bundle upload manifests, creates
// absolute paths for each artifact by appending the |buildDir|, and sets
// a destination path in GCS inside |outDir|.
func ProductBundle2Uploads(mods *build.Modules, namespace string) ([]Upload, error) {
	return productBundle2Uploads(mods, namespace)
}

func productBundle2Uploads(mods productBundlesModules, namespace string) ([]Upload, error) {
	// There should be either 0 or 1 ProductBundles.
	if len(mods.ProductBundles()) == 0 {
		return []Upload{}, nil
	} else if len(mods.ProductBundles()) == 1 {
		return uploadProductBundle(mods, mods.ProductBundles()[0].UploadManifestPath, namespace)
	} else {
		return nil, fmt.Errorf("expected 0 or 1 ProductBundles, found %d", len(mods.ProductBundles()))
	}
}

// Return a list of Uploads that must happen for a specific product bundle
// upload manifest.
func uploadProductBundle(mods productBundlesModules, uploadManifestPath string, namespace string) ([]Upload, error) {
	data, err := os.ReadFile(path.Join(mods.BuildDir(), uploadManifestPath))
	if err != nil {
		return nil, fmt.Errorf("failed to read product bundle upload manifest: %w", err)
	}

	var uploadManifest ProductBundleUploadManifest
	err = json.Unmarshal(data, &uploadManifest)
	if err != nil {
		return nil, fmt.Errorf("unable to unmarshal product bundle upload manifest: %w", err)
	}
	if uploadManifest.Version != "1" {
		return nil, fmt.Errorf("product bundle upload manifest must be version 1")
	}

	var uploads []Upload
	for _, entry := range uploadManifest.Entries {
		uploads = append(uploads, Upload{
			Source:      path.Join(mods.BuildDir(), entry.Source),
			Destination: path.Join(namespace, entry.Destination),
		})
	}
	return uploads, nil
}
