// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"fmt"
	"os"
	"path/filepath"

	"golang.org/x/crypto/ssh"

	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/paver"
	"fuchsia.googlesource.com/host_target_testing/util"
)

type Build interface {
	// GetPackageRepository returns a Repository for this build.
	GetPackageRepository(ctx context.Context) (*packages.Repository, error)

	// GetPaver downloads and returns a paver for the build.
	GetPaver(ctx context.Context) (*paver.Paver, error)
}

type ArchiveBuild struct {
	id              string
	archive         *Archive
	dir             string
	packages        *packages.Repository
	buildArchiveDir string
	sshPublicKey    ssh.PublicKey
}

// GetPackageRepository returns a Repository for this build.
func (b *ArchiveBuild) GetPackageRepository(ctx context.Context) (*packages.Repository, error) {
	if b.packages != nil {
		return b.packages, nil
	}

	path, err := b.archive.download(ctx, b.dir, b.id, "packages.tar.gz")
	if err != nil {
		return nil, fmt.Errorf("failed to download packages.tar.gz: %s", err)
	}

	packagesDir := filepath.Join(b.dir, b.id, "packages")

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
func (b *ArchiveBuild) GetBuildArchive(ctx context.Context) (string, error) {
	if b.buildArchiveDir != "" {
		return b.buildArchiveDir, nil
	}
	path, err := b.archive.download(ctx, b.dir, b.id, "build-archive.tgz")
	if err != nil {
		return "", fmt.Errorf("failed to download build-archive.tar.gz: %s", err)
	}

	buildArchiveDir := filepath.Join(b.dir, b.id, "build-archive")

	if err := os.MkdirAll(buildArchiveDir, 0755); err != nil {
		return "", err
	}

	if err := util.Untar(ctx, buildArchiveDir, path); err != nil {
		return "", fmt.Errorf("failed to extract packages: %s", err)
	}

	b.buildArchiveDir = buildArchiveDir

	return b.buildArchiveDir, nil
}

// GetPaver downloads and returns a paver for the build.
func (b *ArchiveBuild) GetPaver(ctx context.Context) (*paver.Paver, error) {
	buildArchiveDir, err := b.GetBuildArchive(ctx)
	if err != nil {
		return nil, err
	}

	paveScript := filepath.Join(buildArchiveDir, "pave.sh")
	paveZedbootScript := filepath.Join(buildArchiveDir, "pave-zedboot.sh")

	return paver.NewPaver(paveZedbootScript, paveScript, b.sshPublicKey), nil
}

func (b *ArchiveBuild) Pave(ctx context.Context, deviceName string) error {
	paver, err := b.GetPaver(ctx)
	if err != nil {
		return err
	}

	return paver.Pave(ctx, deviceName)
}

func (b *ArchiveBuild) String() string {
	return b.id
}

type FuchsiaDirBuild struct {
	dir          string
	sshPublicKey ssh.PublicKey
}

func NewFuchsiaDirBuild(dir string, publicKey ssh.PublicKey) *FuchsiaDirBuild {
	return &FuchsiaDirBuild{dir: dir, sshPublicKey: publicKey}
}

func (b *FuchsiaDirBuild) String() string {
	return b.dir
}

func (b *FuchsiaDirBuild) GetPackageRepository(ctx context.Context) (*packages.Repository, error) {
	return packages.NewRepository(filepath.Join(b.dir, "amber-files"))
}

func (b *FuchsiaDirBuild) GetPaver(ctx context.Context) (*paver.Paver, error) {
	return paver.NewPaver(
		filepath.Join(b.dir, "pave-zedboot.sh"),
		filepath.Join(b.dir, "pave.sh"),
		b.sshPublicKey,
	), nil
}
