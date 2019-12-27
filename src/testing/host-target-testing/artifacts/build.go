// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/paver"
	"fuchsia.googlesource.com/host_target_testing/util"
	"golang.org/x/crypto/ssh"
)

type Build struct {
	ID       string
	archive  *Archive
	packages *packages.Repository
	snapshot *SystemSnapshot
	dir      string
}

// GetPackageRepository returns a Repository for this build.
func (b *Build) GetPackageRepository() (*packages.Repository, error) {
	if b.packages != nil {
		return b.packages, nil
	}

	path, err := b.archive.download(b.dir, b.ID, "packages.tar.gz")
	if err != nil {
		return nil, fmt.Errorf("failed to download packages.tar.gz: %s", err)
	}

	packagesDir := filepath.Join(b.dir, b.ID, "packages")

	if err := os.MkdirAll(packagesDir, 0755); err != nil {
		return nil, err
	}

	p, err := packages.NewRepositoryFromTar(packagesDir, path)
	if err != nil {
		return nil, err
	}
	b.packages = p

	return b.packages, nil
}

// GetBuildArchive downloads and extracts the build-artifacts.tgz from the
// build id `buildId`. Returns a path to the directory of the extracted files,
// or an error if it fails to download or extract.
func (b *Build) GetBuildArchive() (string, error) {
	path, err := b.archive.download(b.dir, b.ID, "build-archive.tgz")
	if err != nil {
		return "", fmt.Errorf("failed to download build-archive.tar.gz: %s", err)
	}

	buildArchiveDir := filepath.Join(b.dir, b.ID, "build-archive")

	if err := os.MkdirAll(buildArchiveDir, 0755); err != nil {
		return "", err
	}

	if err := util.Untar(buildArchiveDir, path); err != nil {
		return "", fmt.Errorf("failed to extract packages: %s", err)
	}

	return buildArchiveDir, nil
}

// GetPaver downloads and returns a paver for the build.
func (b *Build) GetPaver(publicKey ssh.PublicKey) (*paver.Paver, error) {
	buildArchiveDir, err := b.GetBuildArchive()
	if err != nil {
		return nil, err
	}

	paveScript := filepath.Join(buildArchiveDir, "pave.sh")
	paveZedbootScript := filepath.Join(buildArchiveDir, "pave-zedboot.sh")

	return paver.NewPaver(paveZedbootScript, paveScript, publicKey), nil
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

// GetSystemSnapshot downloads and returns the system snapshot for the build.
func (b *Build) GetSystemSnapshot() (*SystemSnapshot, error) {
	if b.snapshot != nil {
		return b.snapshot, nil
	}

	path, err := b.archive.download(b.dir, b.ID, "system.snapshot.json")
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

	b.snapshot = snapshot

	return b.snapshot, nil
}

func (b *Build) String() string {
	return b.ID
}
