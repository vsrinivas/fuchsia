// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package publish

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"testing"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/repo"
)

func TestPublishArchive(t *testing.T) {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	depfilePath := filepath.Join(cfg.OutputDir, "depfile.d")

	archivePath, err := makePackageArchive(cfg)
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(archivePath)

	repoDir, err := ioutil.TempDir("", "pm-publish-test-repo")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(repoDir)

	if err := Run(cfg, []string{"-repo", repoDir, "-depfile", depfilePath, "-a", "-f", archivePath}); err != nil {
		t.Fatal(err)
	}

	for _, jsonPath := range []string{"timestamp.json", "targets.json", "snapshot.json"} {
		f, err := os.Open(filepath.Join(repoDir, "repository", jsonPath))
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		m := make(map[string]json.RawMessage)
		if err := json.NewDecoder(f).Decode(&m); err != nil {
			t.Fatal(err)
		}
	}

	assertHasTestPackage(t, repoDir)

	outName, inputPaths := readDepfile(depfilePath)
	if ex := filepath.Join(repoDir, "repository", "timestamp.json"); ex != outName {
		t.Errorf("depfile output: got %q, want %q", outName, ex)
	}

	if len(inputPaths) != 1 {
		t.Fatalf("got %#v, wanted one input", inputPaths)
	}

	if inputPaths[0] != archivePath {
		t.Errorf("depfile inputs: %#v != %#v", inputPaths[0], archivePath)
	}
}

func TestPublishListOfPackages(t *testing.T) {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.BuildTestPackage(cfg)

	depfilePath := filepath.Join(cfg.OutputDir, "depfile.d")

	outputManifestPath := filepath.Join(cfg.OutputDir, "package_manifest.json")
	packagesListPath := filepath.Join(cfg.OutputDir, "packages.list")

	if err := ioutil.WriteFile(packagesListPath, []byte(outputManifestPath+"\n"), os.ModePerm); err != nil {
		t.Fatal(err)
	}

	repoDir, err := ioutil.TempDir("", "pm-publish-test-repo")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(repoDir)

	if err := Run(cfg, []string{"-repo", repoDir, "-depfile", depfilePath, "-lp", "-f", packagesListPath}); err != nil {
		t.Fatal(err)
	}

	assertHasTestPackage(t, repoDir)

	outName, inputPaths := readDepfile(depfilePath)
	if ex := filepath.Join(repoDir, "repository", "timestamp.json"); ex != outName {
		t.Errorf("depfile output: got %q, want %q", outName, ex)
	}

	if inputPaths[0] != packagesListPath {
		t.Errorf("depfile inputs: %q != %q", inputPaths[0], packagesListPath)
	}
	if inputPaths[1] != outputManifestPath {
		t.Errorf("depfile inputs: %q != %q", inputPaths[1], outputManifestPath)
	}

	inputPaths = inputPaths[2:]
	sort.Strings(inputPaths)

	blobs, err := cfg.BlobInfo()
	if err != nil {
		t.Fatal(err)
	}
	if len(inputPaths) != len(blobs) {
		t.Errorf("deps entries: %#v != %#v", inputPaths, blobs)
	}
	sourcePaths := []string{}
	for _, blob := range blobs {
		sourcePaths = append(sourcePaths, blob.SourcePath)
	}
	sort.Strings(sourcePaths)

	for i := range sourcePaths {
		if inputPaths[i] != sourcePaths[i] {
			t.Errorf("deps entry: %q != %q", inputPaths[i], sourcePaths[i])
		}
	}
}

func readDepfile(depfilePath string) (string, []string) {
	b, err := ioutil.ReadFile(depfilePath)
	if err != nil {
		panic(err)
	}
	parts := bytes.SplitN(b, []byte(": "), 2)
	outName := string(parts[0])
	inputs := bytes.Split(bytes.TrimSpace(parts[1]), []byte(" "))
	inputPaths := []string{}
	for _, input := range inputs {
		inputPaths = append(inputPaths, string(input))
	}
	for i, input := range inputPaths {
		var err error
		inputPaths[i], err = strconv.Unquote(input)
		if err != nil {
			panic(err)
		}
	}
	return outName, inputPaths
}

func assertHasTestPackage(t *testing.T, repoDir string) {
	repo, err := repo.New(repoDir)
	if err != nil {
		panic(err)
	}
	dataFiles, err := repo.Targets()
	if err != nil {
		panic(err)
	}
	if _, ok := dataFiles["testpackage/0"]; !ok {
		t.Fatalf("package not found: %q in %#v", "testpackage", dataFiles)
	}
}

func makePackageArchive(cfg *build.Config) (string, error) {
	build.BuildTestPackage(cfg)
	f, err := ioutil.TempFile("", "testpackage-0")
	if err != nil {
		return "", err
	}
	f.Close()
	return f.Name() + ".far", build.Archive(cfg, f.Name())
}
