// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"fmt"
	"os"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

type TransferManifest struct {
	Version string                  `json:"version"`
	Entries []TransferManifestEntry `json:"entries"`
}

type TransferManifestEntry struct {
	Type    string          `json:"type"`
	Local   string          `json:"local"`
	Remote  string          `json:"remote"`
	Entries []ArtifactEntry `json:"entries"`
}

type ArtifactEntry struct {
	Name string `json:"name"`
}

type productBundlesModules interface {
	BuildDir() string
	ProductBundles() []build.ProductBundle
}

// ProductBundle2Uploads parses the product bundle upload manifests, creates
// absolute paths for each artifact by appending the |buildDir|, and sets
// a destination path in GCS inside |outDir|.
func ProductBundle2Uploads(mods *build.Modules, blobsRemote string, productBundleRemote string) ([]Upload, error) {
	return productBundle2Uploads(mods, blobsRemote, productBundleRemote)
}

func productBundle2Uploads(mods productBundlesModules, blobsRemote string, productBundleRemote string) ([]Upload, error) {
	// There should be either 0 or 1 ProductBundles.
	if len(mods.ProductBundles()) == 0 {
		return []Upload{}, nil
	} else if len(mods.ProductBundles()) == 1 {
		return uploadProductBundle(mods, mods.ProductBundles()[0].TransferManifestPath, blobsRemote, productBundleRemote)
	} else {
		return nil, fmt.Errorf("expected 0 or 1 ProductBundles, found %d", len(mods.ProductBundles()))
	}
}

// Return a list of Uploads that must happen for a specific product bundle
// transfer manifest.
func uploadProductBundle(mods productBundlesModules, transferManifestPath string, blobsRemote string, productBundleRemote string) ([]Upload, error) {
	transferManifestParentPath := filepath.Dir(transferManifestPath)

	data, err := os.ReadFile(path.Join(mods.BuildDir(), transferManifestPath))
	if err != nil {
		return nil, fmt.Errorf("failed to read product bundle transfer manifest: %w", err)
	}

	var transferManifest TransferManifest
	err = json.Unmarshal(data, &transferManifest)
	if err != nil {
		return nil, fmt.Errorf("unable to unmarshal product bundle transfer manifest: %w", err)
	}
	if transferManifest.Version != "1" {
		return nil, fmt.Errorf("product bundle transfer manifest must be version 1")
	}

	var uploads []Upload
	var newTransferEntries []TransferManifestEntry
	for _, entry := range transferManifest.Entries {
		remote := ""
		if entry.Type == "product_bundle" {
			remote = productBundleRemote
			for _, artifact := range entry.Entries {
				uploads = append(uploads, Upload{
					Source:      path.Join(mods.BuildDir(), transferManifestParentPath, entry.Local, artifact.Name),
					Destination: path.Join(remote, entry.Remote, artifact.Name),
				})
			}
		} else if entry.Type == "blobs" {
			remote = blobsRemote
			uploads = append(uploads, Upload{
				Source:      path.Join(mods.BuildDir(), transferManifestParentPath, entry.Local),
				Destination: path.Join(remote, entry.Remote),
				Deduplicate: true,
			})
		} else {
			return nil, fmt.Errorf("unrecognized transfer entry type: %s", entry.Type)
		}

		// Modify the remote inside the entry, so that we can upload this transfer
		// manifest and use it to download the artifacts.
		entry.Remote = path.Join(remote, entry.Remote)
		newTransferEntries = append(newTransferEntries, entry)
	}

	// Upload the transfer manifest itself so that it can be used for downloading
	// the artifacts.
	transferManifest.Entries = newTransferEntries
	updatedTransferManifest, err := json.MarshalIndent(&transferManifest, "", "  ")
	if err != nil {
		return nil, err
	}
	uploads = append(uploads, Upload{
		Compress:    true,
		Contents:    updatedTransferManifest,
		Destination: path.Join(productBundleRemote, filepath.Base(transferManifestPath)),
	})

	return uploads, nil
}
