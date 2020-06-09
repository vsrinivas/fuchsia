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

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/avb"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/paver"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
)

type Build interface {
	// GetPackageRepository returns a Repository for this build.
	GetPackageRepository(ctx context.Context) (*packages.Repository, error)

	// GetPaverDir downloads and returns the directory containing the paver scripts.
	GetPaverDir(ctx context.Context) (string, error)

	// GetPaver downloads and returns a paver for the build.
	GetPaver(ctx context.Context) (paver.Paver, error)

	// GetSshPublicKey returns the SSH public key used by this build's paver.
	GetSshPublicKey() ssh.PublicKey

	// GetVbmetaPath downloads and returns a path to the zircon-a vbmeta image.
	GetVbmetaPath(ctx context.Context) (string, error)
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
		return nil, fmt.Errorf("failed to download packages.tar.gz: %w", err)
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
		return "", fmt.Errorf("failed to download build-archive.tar.gz: %w", err)
	}

	buildArchiveDir := filepath.Join(b.dir, b.id, "build-archive")

	if err := os.MkdirAll(buildArchiveDir, 0755); err != nil {
		return "", err
	}

	if err := util.Untar(ctx, buildArchiveDir, path); err != nil {
		return "", fmt.Errorf("failed to extract packages: %w", err)
	}

	b.buildArchiveDir = buildArchiveDir

	return b.buildArchiveDir, nil
}

func (b *ArchiveBuild) GetPaverDir(ctx context.Context) (string, error) {
	return b.GetBuildArchive(ctx)
}

// GetPaver downloads and returns a paver for the build.
func (b *ArchiveBuild) GetPaver(ctx context.Context) (paver.Paver, error) {
	buildArchiveDir, err := b.GetBuildArchive(ctx)
	if err != nil {
		return nil, err
	}

	paveScript := filepath.Join(buildArchiveDir, "pave.sh")
	paveZedbootScript := filepath.Join(buildArchiveDir, "pave-zedboot.sh")

	return paver.NewBuildPaver(paveZedbootScript, paveScript, paver.SSHPublicKey(b.sshPublicKey))
}

func (b *ArchiveBuild) GetSshPublicKey() ssh.PublicKey {
	return b.sshPublicKey
}

func (b *ArchiveBuild) GetVbmetaPath(ctx context.Context) (string, error) {
	buildArchiveDir, err := b.GetBuildArchive(ctx)
	if err != nil {
		return "", err
	}
	return filepath.Join(buildArchiveDir, "zircon-a.vbmeta"), nil
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

func (b *FuchsiaDirBuild) GetPaverDir(ctx context.Context) (string, error) {
	return b.dir, nil
}

func (b *FuchsiaDirBuild) GetPaver(ctx context.Context) (paver.Paver, error) {
	return paver.NewBuildPaver(
		filepath.Join(b.dir, "pave-zedboot.sh"),
		filepath.Join(b.dir, "pave.sh"),
		paver.SSHPublicKey(b.sshPublicKey),
	)
}

func (b *FuchsiaDirBuild) GetSshPublicKey() ssh.PublicKey {
	return b.sshPublicKey
}

func (b *FuchsiaDirBuild) GetVbmetaPath(ctx context.Context) (string, error) {
	return filepath.Join(b.dir, "fuchsia.vbmeta"), nil
}

type OmahaBuild struct {
	build    Build
	omahaUrl string
	avbtool  *avb.AVBTool
}

func NewOmahaBuild(build Build, omahaUrl string, avbtool *avb.AVBTool) *OmahaBuild {
	return &OmahaBuild{build: build, omahaUrl: omahaUrl, avbtool: avbtool}
}

// GetPackageRepository returns a Repository for this build.
func (b *OmahaBuild) GetPackageRepository(ctx context.Context) (*packages.Repository, error) {
	return b.build.GetPackageRepository(ctx)
}

func (b *OmahaBuild) GetPaverDir(ctx context.Context) (string, error) {
	return b.build.GetPaverDir(ctx)
}

// GetPaver downloads and returns a paver for the build.
func (b *OmahaBuild) GetPaver(ctx context.Context) (paver.Paver, error) {
	paverDir, err := b.GetPaverDir(ctx)
	if err != nil {
		return nil, err
	}

	paveScript := filepath.Join(paverDir, "pave.sh")
	paveZedbootScript := filepath.Join(paverDir, "pave-zedboot.sh")

	bootArgs := map[string]string{
		"omaha_url": b.omahaUrl,
	}
	destVbmeta := filepath.Join(paverDir, "zircon-a-omaha-test.vbmeta")
	srcVbmeta, err := b.GetVbmetaPath(ctx)
	if err != nil {
		return nil, err
	}

	err = b.avbtool.MakeVBMetaImage(ctx, destVbmeta, srcVbmeta, bootArgs)
	if err != nil {
		return nil, err
	}

	return paver.NewBuildPaver(
		paveZedbootScript,
		paveScript,
		paver.SSHPublicKey(b.GetSshPublicKey()),
		paver.OverrideVBMetaA(destVbmeta),
	)
}

func (b *OmahaBuild) GetSshPublicKey() ssh.PublicKey {
	return b.build.GetSshPublicKey()
}

func (b *OmahaBuild) GetVbmetaPath(ctx context.Context) (string, error) {
	return b.build.GetVbmetaPath(ctx)
}
