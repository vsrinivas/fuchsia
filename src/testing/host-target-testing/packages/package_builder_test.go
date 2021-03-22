// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"bytes"
	"context"
	"encoding/json"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
)

// createTestPackage fills the given directory with a new repository.
func createTestPackage(t *testing.T, dir string) (*Repository, string) {
	ctx := context.Background()

	// Initialize a repo.
	t.Logf("Creating repo at %s", dir)
	pmRepo, err := repo.New(dir)
	if err != nil {
		t.Fatal(err)
	}
	if err = pmRepo.Init(); err != nil {
		t.Fatal(err)
	}

	// Create a config.
	config := build.TestConfig()
	t.Logf("Creating meta.far in %s", config.OutputDir)
	build.BuildTestPackage(config)
	defer os.RemoveAll(filepath.Dir(config.OutputDir))

	// Grab the merkle of the config's package manifest.
	manifestDir := filepath.Join(config.OutputDir, "package_manifest.json")
	manifest, err := ioutil.ReadFile(manifestDir)
	if err != nil {
		t.Fatal(err)
	}
	var packageManifest build.PackageManifest
	if err := json.Unmarshal(manifest, &packageManifest); err != nil {
		t.Fatal(err)
	}
	metaMerkle := ""
	for _, blob := range packageManifest.Blobs {
		if blob.Path == "meta/" {
			metaMerkle = blob.Merkle.String()
		}
	}
	if metaMerkle == "" {
		t.Fatal("did not find meta.far in manifest")
	}

	// Publish the config to the repo.
	if _, err = pmRepo.PublishManifest(manifestDir); err != nil {
		t.Fatalf("failed to publish manifest: %s", err)
	}
	if err = pmRepo.CommitUpdates(true); err != nil {
		t.Fatalf("failed to commit updates to repo: %s", err)
	}
	pkgRepo, err := NewRepository(ctx, dir)
	if err != nil {
		t.Fatalf("failed to read repo: %s", err)
	}
	return pkgRepo, metaMerkle
}

// expandPackage expands the given merkle from the given repository into the given directory.
func expandPackage(t *testing.T, pkgRepo *Repository, merkle string, dir string) {
	// Parse the package we want.
	pkg, err := newPackage(pkgRepo, merkle)
	if err != nil {
		t.Fatalf("failed to read package: %s", err)
	}

	// Expand to the given directory.
	if err = pkg.Expand(dir); err != nil {
		t.Fatalf("failed to expand to dir: %s", err)
	}
}

// createAndExpandPackage creates temporary directories and expands a test package, returning the expand directory.
func createAndExpandPackage(t *testing.T, parentDir string) (*Repository, string) {
	dir := filepath.Join(parentDir, "package")
	if err := os.Mkdir(dir, 0o700); err != nil {
		t.Fatal(err)
	}
	pkgRepo, metaMerkle := createTestPackage(t, dir)
	expand := filepath.Join(parentDir, "expand")
	if err := os.Mkdir(expand, 0o700); err != nil {
		t.Fatal(err)
	}
	expandPackage(t, pkgRepo, metaMerkle, expand)
	return pkgRepo, expand
}

func TestAddResource(t *testing.T) {
	parentDir := t.TempDir()
	_, expandDir := createAndExpandPackage(t, parentDir)
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
			t.Errorf("File %s is not expected", key)
		}
	}

	if len(expectedFiles) != len(pkgBuilder.Contents) {
		t.Errorf("Package contents has %d files, should have %d", len(pkgBuilder.Contents), len(expectedFiles))
	}

	if err := pkgBuilder.AddResource(newResource, bytes.NewReader([]byte(newResource))); err == nil {
		t.Fatalf("Resource %s should have failed to be added twice.", newResource)
	}
}

func TestPublish(t *testing.T) {
	ctx := context.Background()
	parentDir := t.TempDir()
	pkgRepo, expandDir := createAndExpandPackage(t, parentDir)
	pkgBuilder, err := NewPackageBuilderFromDir(expandDir, "testpackage", "0")
	if err != nil {
		t.Fatalf("Failed to parse package from %s. %s", expandDir, err)
	}
	defer pkgBuilder.Close()

	fullPkgName := pkgBuilder.Name + "/" + pkgBuilder.Version
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
