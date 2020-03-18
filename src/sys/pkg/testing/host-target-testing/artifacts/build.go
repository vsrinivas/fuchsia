// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
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
	dir      string
}

// GetPackageRepository returns a Repository for this build.
func (b *Build) GetPackageRepository(ctx context.Context) (*packages.Repository, error) {
	if b.packages != nil {
		return b.packages, nil
	}

	path, err := b.archive.download(ctx, b.dir, b.ID, "packages.tar.gz")
	if err != nil {
		return nil, fmt.Errorf("failed to download packages.tar.gz: %s", err)
	}

	packagesDir := filepath.Join(b.dir, b.ID, "packages")

	if err := os.MkdirAll(packagesDir, 0755); err != nil {
		return nil, err
	}

	p, err := packages.NewRepositoryFromTar(ctx, packagesDir, path)
	if err != nil {
		return nil, err
	}
	b.packages = p

	return b.packages, nil
}

// GetBuildArchive downloads and extracts the build-artifacts.tgz from the
// build id `buildId`. Returns a path to the directory of the extracted files,
// or an error if it fails to download or extract.
func (b *Build) GetBuildArchive(ctx context.Context) (string, error) {
	path, err := b.archive.download(ctx, b.dir, b.ID, "build-archive.tgz")
	if err != nil {
		return "", fmt.Errorf("failed to download build-archive.tar.gz: %s", err)
	}

	buildArchiveDir := filepath.Join(b.dir, b.ID, "build-archive")

	if err := os.MkdirAll(buildArchiveDir, 0755); err != nil {
		return "", err
	}

	if err := util.Untar(ctx, buildArchiveDir, path); err != nil {
		return "", fmt.Errorf("failed to extract packages: %s", err)
	}

	return buildArchiveDir, nil
}

// GetPaver downloads and returns a paver for the build.
func (b *Build) GetPaver(ctx context.Context, publicKey ssh.PublicKey) (*paver.Paver, error) {
	buildArchiveDir, err := b.GetBuildArchive(ctx)
	if err != nil {
		return nil, err
	}

	paveScript := filepath.Join(buildArchiveDir, "pave.sh")
	paveZedbootScript := filepath.Join(buildArchiveDir, "pave-zedboot.sh")

	return paver.NewPaver(paveZedbootScript, paveScript, publicKey), nil
}

func (b *Build) String() string {
	return b.ID
}
