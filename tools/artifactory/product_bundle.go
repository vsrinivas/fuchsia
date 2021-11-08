// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

const (
	// productBundleName is the canonical name of product bundle in the image manifest.
	productBundleName = "product_bundle"

	// productBundleType is the canonical type of product bundle in the image manifest.
	productBundleType = "manifest"

	// fileURIPrefix is the prefix for the file URI scheme.
	fileURIPrefix = "file:/"
)

// ProductBundle is a struct that contains a collection of images and packages
// that are outputted from a product build.
type ProductBundle struct {
	Data     Data   `json:"data"`
	SchemaID string `json:"schema_id"`
}

// Data contained in the product bundle.
type Data struct {
	DeviceRefs  json.RawMessage `json:"device_refs"`
	Images      []*Image        `json:"images"`
	Type        json.RawMessage `json:"type"`
	Name        json.RawMessage `json:"name"`
	Packages    []*Package      `json:"packages"`
	Description json.RawMessage `json:"description"`
	Metadata    json.RawMessage `json:"metadata,omitempty"`
	Manifests   json.RawMessage `json:"manifests,omitempty"`
}

// A set of artifacts necessary to run a physical or virtual device.
type Package struct {
	Format  string `json:"format"`
	BlobURI string `json:"blob_uri,omitempty"`
	RepoURI string `json:"repo_uri"`
}

// A set of artifacts necessary to provision a physical or virtual device.
type Image struct {
	BaseURI string `json:"base_uri"`
	Format  string `json:"format"`
}

// updateProductManifest reads the product bundle from the build_dir and updates it to match
// the relative paths used by artifactory.
func updateProductManifest(buildDir, productBundlePath, packageNamespaceDir, blobNamespaceDir, imageNamespaceDir string) ([]byte, error) {
	productBundleAbsPath := filepath.Join(buildDir, productBundlePath)
	data, err := ioutil.ReadFile(productBundleAbsPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read product bundle: %w", err)
	}

	var productBundle ProductBundle
	err = json.Unmarshal(data, &productBundle)
	if err != nil {
		return nil, fmt.Errorf("unable to unmarshal product bundle: %w", err)
	}

	artifactoryProductBundlePath := filepath.Join(imageNamespaceDir, productBundlePath)
	for _, image := range productBundle.Data.Images {
		imageURI, err := filepath.Rel(artifactoryProductBundlePath, imageNamespaceDir)
		if err != nil {
			return nil, fmt.Errorf("unable to find relative path for artifactory product bundle path %s and image namespace %s: %w", artifactoryProductBundlePath, imageNamespaceDir, err)
		}
		image.BaseURI = fileURIPrefix + imageURI
	}

	for _, pkg := range productBundle.Data.Packages {
		repoURI, err := filepath.Rel(artifactoryProductBundlePath, packageNamespaceDir)
		if err != nil {
			return nil, fmt.Errorf("unable to find relative path for artifactory product bundle path %s and package namespace %s: %w", artifactoryProductBundlePath, packageNamespaceDir, err)
		}

		blobURI, err := filepath.Rel(artifactoryProductBundlePath, blobNamespaceDir)
		if err != nil {
			return nil, fmt.Errorf("unable to find relative path for artifactory product bundle path %s and blob namespace %s: %w", artifactoryProductBundlePath, blobNamespaceDir, err)
		}
		pkg.RepoURI = fileURIPrefix + repoURI
		pkg.BlobURI = fileURIPrefix + blobURI
	}

	return json.MarshalIndent(&productBundle, "", "  ")
}

// ProductBundleUploads parses the image manifest located in the build and returns a list of Uploads
// containing only product bundles with updated relative URI's based on artifactory's structure.
func ProductBundleUploads(mods *build.Modules, packageNamespaceDir, blobNamespaceDir, imageNamespaceDir string) (*Upload, error) {
	return productBundleUploads(mods, packageNamespaceDir, blobNamespaceDir, imageNamespaceDir)
}

func productBundleUploads(mods imgModules, packageNamespaceDir, blobNamespaceDir, imageNamespaceDir string) (*Upload, error) {
	for _, img := range mods.Images() {
		if img.Name != productBundleName || img.Type != productBundleType {
			continue
		}
		updatedProductBundle, err := updateProductManifest(mods.BuildDir(), img.Path, packageNamespaceDir, blobNamespaceDir, imageNamespaceDir)
		if err != nil {
			return nil, err
		}
		// Return early since there should be a maximum of 1 product bundle entry in images.json.
		return &Upload{
			Contents:    updatedProductBundle,
			Destination: path.Join(imageNamespaceDir, img.Path),
			Compress:    true,
		}, nil
	}
	// Product bundle doesn't exist for "bringup" or builds that don't have
	// the board configured (SDK builds for example).
	return nil, nil
}
