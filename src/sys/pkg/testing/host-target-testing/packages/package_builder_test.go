// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
)

// createTestPackage fills the given directory with a new repository.
func createTestPackage(dir string) (*Repository, string, error) {
	ctx := context.Background()

	// Initialize a repo.
	fmt.Printf("Creating repo at %s.\n", dir)
	pmRepo, err := repo.New(dir)
	if err != nil {
		return nil, "", fmt.Errorf("failed to create repo. %w", err)
	}
	if err = pmRepo.Init(); err != nil {
		return nil, "", fmt.Errorf("failed to init repo. %w", err)
	}

	// Create a config.
	config := build.TestConfig()
	log.Printf("Creating meta.far in %s", config.OutputDir)
	build.BuildTestPackage(config)
	defer os.RemoveAll(filepath.Dir(config.OutputDir))

	// Grab the merkle of the config's package manifest.
	manifestDir := filepath.Join(config.OutputDir, "package_manifest.json")
	manifest, err := ioutil.ReadFile(manifestDir)
	var packageManifest build.PackageManifest
	if err := json.Unmarshal(manifest, &packageManifest); err != nil {
		return nil, "", fmt.Errorf("could not decode package_manifest.json. %w", err)
	}
	metaMerkle := ""
	for _, blob := range packageManifest.Blobs {
		if blob.Path == "meta/" {
			metaMerkle = blob.Merkle.String()
		}
	}
	if metaMerkle == "" {
		return nil, "", fmt.Errorf("did not find meta.far in manifest")
	}

	// Publish the config to the repo.
	_, err = pmRepo.PublishManifest(manifestDir)
	if err != nil {
		return nil, "", fmt.Errorf("failed to publish manifest. %w", err)
	}
	if err = pmRepo.CommitUpdates(true); err != nil {
		return nil, "", fmt.Errorf("failed to commit updates to repo. %w", err)
	}
	pkgRepo, err := NewRepository(ctx, dir)
	if err != nil {
		return nil, "", fmt.Errorf("failed to read repo. %w", err)
	}

	return pkgRepo, metaMerkle, nil
}

// expandPackage expands the given merkle from the given repository into the given directory.
func expandPackage(pkgRepo *Repository, merkle string, dir string) error {
	// Parse the package we want.
	pkg, err := newPackage(pkgRepo, merkle)
	if err != nil {
		return fmt.Errorf("failed to read package. %w", err)
	}

	// Expand to the given directory.
	if err = pkg.Expand(dir); err != nil {
		return fmt.Errorf("failed to expand to dir. %w", err)
	}
	return nil
}

// createAndExpandPackage creates temporary directories and expands a test package, returning the expand directory.
func createAndExpandPackage(parentDir string) (*Repository, string, error) {
	dir, err := ioutil.TempDir(parentDir, "package")
	if err != nil {
		return nil, "", fmt.Errorf("Failed to create directory %s. %w", dir, err)
	}
	pkgRepo, metaMerkle, err := createTestPackage(dir)
	if err != nil {
		return nil, "", fmt.Errorf("Failed to create test package. %w", err)
	}
	dir, err = ioutil.TempDir(parentDir, "expand")
	if err != nil {
		return nil, "", fmt.Errorf("Failed to create directory %s. %w", dir, err)
	}
	err = expandPackage(pkgRepo, metaMerkle, dir)
	if err != nil {
		return nil, "", fmt.Errorf("Failed to expand package to directory %s. %w", dir, err)
	}
	return pkgRepo, dir, nil
}

func TestAddResource(t *testing.T) {
	parentDir := filepath.Join("", "omaha-pkg-test-add-resource")
	if err := os.MkdirAll(parentDir, 0755); err != nil {
		t.Fatalf("Failed to create directory %s, %s", parentDir, err)
	}
	defer os.RemoveAll(parentDir)

	_, expandDir, err := createAndExpandPackage(parentDir)
	if err != nil {
		t.Fatalf("Failed to create and expand package. %s", err)
	}

	pkgBuilder, err := NewPackageBuilderFromDir(expandDir, "testpackage", "0")
	if err != nil {
		t.Fatalf("Failed to parse package from %s. %s", expandDir, err)
	}
	defer pkgBuilder.Close()

	newResource := "blah/z"

	// Confirm the new resource doesn't exist yet.
	if _, ok := pkgBuilder.Contents[newResource]; ok {
		t.Fatalf("Test resource %s should not exist yet in the package.", newResource)
	}

	pkgBuilder.AddResource(newResource, bytes.NewReader([]byte(newResource)))

	// Confirm the file and contents were added.
	path, ok := pkgBuilder.Contents[newResource]
	if !ok {
		t.Fatalf("Test resource %s failed to be added.", newResource)
	}
	newData, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatalf("Failed to read contents of %s. %s", newResource, err)
	}
	if string(newData) != newResource {
		t.Fatalf("%s expects to have %s, but has %s", newResource, newResource, string(newData))
	}

	expectedFiles := map[string]struct{}{
		"meta/contents": {},
		"meta/foo/one":  {},
		"meta/package":  {},
		"blah/z":        {},
	}
	for _, item := range build.TestFiles {
		expectedFiles[item] = struct{}{}
	}

	for key := range pkgBuilder.Contents {
		if _, ok := expectedFiles[key]; !ok {
			t.Fatalf("File %s is not expected.", key)
		}
	}

	if len(expectedFiles) != len(pkgBuilder.Contents) {
		t.Fatalf("Package contents has %d files, should have %d", len(pkgBuilder.Contents), len(expectedFiles))
	}

	if err := pkgBuilder.AddResource(newResource, bytes.NewReader([]byte(newResource))); err == nil {
		t.Fatalf("Resource %s should have failed to be added twice.", newResource)
	}
}

func TestPublish(t *testing.T) {
	ctx := context.Background()

	parentDir := filepath.Join("", "omaha-pkg-test-publish")
	if err := os.MkdirAll(parentDir, 0755); err != nil {
		t.Fatalf("Failed to create directory %s, %s", parentDir, err)
	}
	defer os.RemoveAll(parentDir)

	pkgRepo, expandDir, err := createAndExpandPackage(parentDir)
	if err != nil {
		t.Fatalf("Failed to create and expand package. %s", err)
	}

	pkgBuilder, err := NewPackageBuilderFromDir(expandDir, "testpackage", "0")
	if err != nil {
		t.Fatalf("Failed to parse package from %s. %s", expandDir, err)
	}
	defer pkgBuilder.Close()

	fullPkgName := fmt.Sprintf("%s/%s", pkgBuilder.Name, pkgBuilder.Version)
	newResource := "blah/z"

	// Confirm package in repo is as expected.
	pkg, err := pkgRepo.OpenPackage(fullPkgName)
	if err != nil {
		t.Fatalf("Repo does not contain '%s'. %s", fullPkgName, err)
	}
	if _, err := pkg.ReadFile(newResource); err == nil {
		t.Fatalf("%s should not be found in package", newResource)
	}

	// Add resource to package.
	pkgBuilder.AddResource(newResource, bytes.NewReader([]byte(newResource)))

	// Update repo with updated package. We don't check the merkle since the package includes randomly generated files.
	actualPkgName, _, err := pkgBuilder.Publish(ctx, pkgRepo)
	if err != nil {
		t.Fatalf("Publishing package failed. %s", err)
	}

	if actualPkgName != fullPkgName {
		t.Fatalf("package path should be %q, not %q", fullPkgName, actualPkgName)
	}

	pkgRepo, err = NewRepository(ctx, path.Dir(pkgRepo.Dir))

	// Confirm that the package is published and updated.
	pkg, err = pkgRepo.OpenPackage(fullPkgName)
	if err != nil {
		t.Fatalf("Repo does not contain '%s'. %s", fullPkgName, err)
	}
	if data, err := pkg.ReadFile(newResource); err != nil {
		t.Fatalf("%s should be in package.", newResource)
	} else {
		if string(data) != newResource {
			t.Fatalf("%s should have value %s but is %s", newResource, newResource, data)
		}
	}
}
