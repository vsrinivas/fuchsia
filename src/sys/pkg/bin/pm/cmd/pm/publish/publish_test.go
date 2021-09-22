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

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
)

func TestPublishArchive(t *testing.T) {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	depfilePath := filepath.Join(cfg.OutputDir, "depfile.d")

	build.BuildTestPackage(cfg)
	name := filepath.Join(t.TempDir(), "testpackage-0")
	if err := ioutil.WriteFile(name, nil, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := build.Archive(cfg, name); err != nil {
		t.Fatal(err)
	}
	archivePath := name + ".far"
	repoDir := t.TempDir()
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

	outName, inputPaths := readDepfile(t, depfilePath)
	if ex := filepath.Join(repoDir, "repository", "timestamp.json"); ex != outName {
		t.Errorf("depfile output: got %q, want %q", outName, ex)
	}

	if len(inputPaths) != 1 {
		t.Fatalf("got %#v, wanted one input", inputPaths)
	}

	if got, want := inputPaths[0], mustRelativePath(t, archivePath); got != want {
		t.Errorf("depfile inputs: got %s, want %s", got, want)
	}
}

func TestPublishListOfPackages(t *testing.T) {
	cfg := build.TestConfig()
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.BuildTestPackage(cfg)

	depfilePath := filepath.Join(cfg.OutputDir, "depfile.d")

	outputManifestPath := filepath.Join(cfg.OutputDir, "package_manifest.json")
	packagesListPath := filepath.Join(cfg.OutputDir, "packages.list")

	if err := ioutil.WriteFile(packagesListPath, []byte(outputManifestPath+"\n"), 0600); err != nil {
		t.Fatal(err)
	}

	repoDir := t.TempDir()
	if err := Run(cfg, []string{"-repo", repoDir, "-depfile", depfilePath, "-lp", "-f", packagesListPath}); err != nil {
		t.Fatal(err)
	}

	assertHasTestPackage(t, repoDir)

	outName, inputPaths := readDepfile(t, depfilePath)
	if ex := filepath.Join(repoDir, "repository", "timestamp.json"); ex != outName {
		t.Errorf("depfile output: got %q, want %q", outName, ex)
	}

	if got, want := inputPaths[0], mustRelativePath(t, packagesListPath); got != want {
		t.Errorf("depfile inputs: got %q, want %q", got, want)
	}
	if got, want := inputPaths[1], mustRelativePath(t, outputManifestPath); got != want {
		t.Errorf("depfile inputs: got %q, want %q", got, want)
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
		sourcePaths = append(sourcePaths, mustRelativePath(t, blob.SourcePath))
	}
	sort.Strings(sourcePaths)

	for i := range sourcePaths {
		if inputPaths[i] != sourcePaths[i] {
			t.Errorf("deps entry: got %q, want %q", inputPaths[i], sourcePaths[i])
		}
	}
}

func TestPublishValidatesPackageName(t *testing.T) {
	cfg := build.TestConfig()
	cfg.PkgName = "INVALIDPACKAGENAME"
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.BuildTestPackage(cfg)

	depfilePath := filepath.Join(cfg.OutputDir, "depfile.d")

	outputManifestPath := filepath.Join(cfg.OutputDir, "package_manifest.json")
	packagesListPath := filepath.Join(cfg.OutputDir, "packages.list")

	if err := ioutil.WriteFile(packagesListPath, []byte(outputManifestPath+"\n"), 0600); err != nil {
		t.Fatal(err)
	}

	repoDir := t.TempDir()
	if err := Run(cfg, []string{"-repo", repoDir, "-depfile", depfilePath, "-lp", "-f", packagesListPath}); err == nil {
		t.Fatalf("Expected invalid package name to generate error.")
	}
}

// mustRelativePath converts the input path relative to the current working
// directory. Input path is unchanged if it's not an absolute path.
func mustRelativePath(t *testing.T, p string) string {
	t.Helper()

	if !filepath.IsAbs(p) {
		return p
	}

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("Failed to get current working directory: %v", err)
	}
	rel, err := filepath.Rel(wd, p)
	if err != nil {
		t.Fatalf("Failed to rebase %s to %s: %v", p, wd, err)
	}
	return rel
}

func readDepfile(t *testing.T, depfilePath string) (string, []string) {
	b, err := ioutil.ReadFile(depfilePath)
	if err != nil {
		t.Fatal(err)
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
		if inputPaths[i], err = strconv.Unquote(input); err != nil {
			t.Fatal(err)
		}
	}
	return outName, inputPaths
}

func assertHasTestPackage(t *testing.T, repoDir string) {
	repo, err := repo.New(repoDir, filepath.Join(repoDir, "repository", "blobs"))
	if err != nil {
		t.Fatal(err)
	}
	dataFiles, err := repo.Targets()
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := dataFiles["testpackage/0"]; !ok {
		t.Fatalf("package not found: %q in %#v", "testpackage", dataFiles)
	}
}
