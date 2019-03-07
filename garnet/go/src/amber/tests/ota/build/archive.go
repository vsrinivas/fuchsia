// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/system_ota_test/util"

	pm_repo "fuchsia.googlesource.com/pm/repo"
)

// Archive allows interacting with the build artifact repository.
type Archive struct {
	// Archive will store all downloaded artifacts into this directory.
	dir string

	// lkgb (typically found in $FUCHSIA_DIR/prebuilt/tools/lkgb/lkgb) is
	// used to look up the latest build id for a given builder.
	lkgbPath string

	// artifacts (typically found in $FUCHSIA_DIR/prebuilt/tools/artifacts/artifacts)
	// is used to download artifacts for a given build id.
	artifactsPath string
}

// NewArchive creates a new BuildArtifact.
func NewArchive(lkgbPath string, artifactsPath string, dir string) Archive {
	return Archive{
		dir:           dir,
		lkgbPath:      lkgbPath,
		artifactsPath: artifactsPath,
	}
}

// LookupBuildID looks up the latest build id for a given builder.
func (a *Archive) LookupBuildID(builderName string) (string, error) {
	stdout, stderr, err := util.RunCommand(a.lkgbPath, builderName)
	if err != nil {
		return "", fmt.Errorf("lkgb failed: %s: %s", err, string(stderr))
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}

// GetPackages downloads and extract the packages.tar.gz repository from the build id
// `buildId`.
func (a *Archive) GetPackages(buildID string) (string, error) {
	path, err := a.download(buildID, "packages.tar.gz")
	if err != nil {
		return "", fmt.Errorf("failed to download packages.tar.gz: %s", err)
	}

	packagesDir := filepath.Join(a.dir, buildID, "packages")
	if err := os.MkdirAll(packagesDir, 0755); err != nil {
		return "", err
	}

	if err := util.Untar(packagesDir, path); err != nil {
		return "", fmt.Errorf("failed to extract packages: %s", err)
	}

	// The repository may have out of date metadata. This updates the repository to
	// the latest version so TUF won't complain about the data being old.
	repo, err := pm_repo.New(filepath.Join(packagesDir, "amber-files"))
	if err != nil {
		return "", err
	}

	if err := repo.CommitUpdates(true); err != nil {
		return "", err
	}

	return packagesDir, nil
}

// SystemSnapshot describes the data in the system.snapshot.json file.
type SystemSnapshot struct {
	BuildID  int      `json:"build_id"`
	Snapshot Snapshot `json:"snapshot"`
}

// Snapshot describes all the packages and blobs in the package repository.
type Snapshot struct {
	Packages map[string]PackageInfo `json:"packages"`
	Blobs    map[string]BlobInfo    `json:"blobs"`
}

// PackageInfo describes an individual package artifacts in the repository.
type PackageInfo struct {
	Files map[string]string `json:"files"`
	Tags  []string          `json:"tags"`
}

// BlobInfo describes all the blobs in the repository.
type BlobInfo struct {
	Size int `json:"size"`
}

// GetSystemSnapshot downloads and extract the system.snapshot.json from the
// build id `buildID`.
func (a *Archive) GetSystemSnapshot(buildID string) (*SystemSnapshot, error) {
	path, err := a.download(buildID, "system.snapshot.json")
	if err != nil {
		return nil, fmt.Errorf("failed to download system.snapshot.json: %s", err)
	}

	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q: %s", path, err)
	}
	defer f.Close()

	type systemSnapshot struct {
		BuildID  int    `json:"build_id"`
		Snapshot string `json:"snapshot"`
	}

	var s systemSnapshot
	if err := json.NewDecoder(f).Decode(&s); err != nil {
		return nil, fmt.Errorf("failed to parse %q: %s", path, err)
	}

	snapshot := &SystemSnapshot{BuildID: s.BuildID}
	if err := json.Unmarshal([]byte(s.Snapshot), &snapshot.Snapshot); err != nil {
		return nil, fmt.Errorf("failed to parse snapshot in %q: %s", path, err)
	}

	return snapshot, nil
}

// Download an artifact from the build id `buildID` named `src` and write it
// into a directory `dst`.
func (a *Archive) download(buildID string, src string) (string, error) {
	basename := filepath.Base(src)
	buildDir := filepath.Join(a.dir, buildID)
	path := filepath.Join(buildDir, basename)

	// Skip downloading if the file is already present in the build dir.
	if _, err := os.Stat(path); err == nil {
		return path, nil
	}

	log.Printf("downloading %s to %s", src, path)

	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return "", err
	}

	// We don't want to leak files if we are interrupted during a download.
	// So we'll initally download into a temporary file, and only once it
	// succeeds do we rename it into the real destination.
	tmpfile, err := ioutil.TempFile(a.dir, basename)
	defer func() {
		if tmpfile != nil {
			os.Remove(tmpfile.Name())
		}
	}()

	args := []string{
		"cp",
		"-build", buildID,
		"-src", src,
		"-dst", tmpfile.Name(),
	}

	_, stderr, err := util.RunCommand(a.artifactsPath, args...)
	if err != nil {
		if len(stderr) != 0 {
			return "", fmt.Errorf("artifacts failed: %s: %s", err, string(stderr))
		}
		return "", fmt.Errorf("artifacts failed: %s", err)
	}

	// Now that we've downloaded the file, do an atomic swap of the filename into place.
	if err := os.Rename(tmpfile.Name(), path); err != nil {
		return "", err
	}
	tmpfile = nil

	return path, nil
}
