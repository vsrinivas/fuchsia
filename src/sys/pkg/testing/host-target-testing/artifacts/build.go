// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/avb"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/paver"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/zbi"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type Build interface {
	// GetBootserver returns the path to the bootserver used for paving.
	GetBootserver(ctx context.Context) (string, error)

	// GetPackageRepository returns a Repository for this build.
	GetPackageRepository(ctx context.Context) (*packages.Repository, error)

	// GetPaverDir downloads and returns the directory containing the images
	// and image manifest.
	GetPaverDir(ctx context.Context) (string, error)

	// GetPaver downloads and returns a paver for the build.
	GetPaver(ctx context.Context) (paver.Paver, error)

	// GetSshPublicKey returns the SSH public key used by this build's paver.
	GetSshPublicKey() ssh.PublicKey

	// GetVbmetaPath downloads and returns a path to the zircon-a vbmeta image.
	GetVbmetaPath(ctx context.Context) (string, error)
}

// ArchiveBuild represents build artifacts constructed from archives produced by
// the build.
// TODO(fxbug.dev/52021): Remove when no longer using archives. Since this is to
// be deprecated, it should only be used as the backupArchiveBuild of an
// ArtifactsBuild and does not completely implement the Build interface.
type ArchiveBuild struct {
	id              string
	archive         *Archive
	dir             string
	packages        *packages.Repository
	buildArchiveDir string
}

// GetPackageRepository returns a Repository for this build constructed from the
// packages.tar.gz archive.
func (b *ArchiveBuild) GetPackageRepository(ctx context.Context) (*packages.Repository, error) {
	if b.packages != nil {
		return b.packages, nil
	}

	archive := "packages.tar.gz"
	path := filepath.Join(b.dir, b.id, archive)
	if err := b.archive.download(ctx, b.id, false, path, []string{archive}); err != nil {
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

// GetBuildArchive downloads and extracts the build-archive.tgz from the
// build id `buildId`. Returns a path to the directory of the extracted files,
// or an error if it fails to download or extract.
func (b *ArchiveBuild) GetBuildArchive(ctx context.Context) (string, error) {
	if b.buildArchiveDir != "" {
		return b.buildArchiveDir, nil
	}
	archive := "build-archive.tgz"
	path := filepath.Join(b.dir, b.id, archive)
	if err := b.archive.download(ctx, b.id, false, path, []string{archive}); err != nil {
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

// ArtifactsBuild represents the build artifacts for a specific build.
type ArtifactsBuild struct {
	backupArchiveBuild *ArchiveBuild
	id                 string
	archive            *Archive
	dir                string
	packages           *packages.Repository
	buildImageDir      string
	sshPublicKey       ssh.PublicKey
}

func (b *ArtifactsBuild) GetBootserver(ctx context.Context) (string, error) {
	buildPaver, err := b.getPaver(ctx)
	if err != nil {
		return "", err
	}
	return buildPaver.BootserverPath, nil
}

type blob struct {
	// Merkle is the merkle associated with a blob.
	Merkle string `json:"merkle"`
}

// GetPackageRepository returns a Repository for this build.
func (b *ArtifactsBuild) GetPackageRepository(ctx context.Context) (*packages.Repository, error) {
	if b.packages != nil {
		return b.packages, nil
	}

	artifact := "packages"
	packagesDir := filepath.Join(b.dir, b.id, artifact)
	if err := b.archive.download(ctx, b.id, false, packagesDir, []string{artifact}); err != nil {
		logger.Infof(ctx, "failed to fetch artifacts for build %d. Using archives.", b.id)
		b.packages, err = b.backupArchiveBuild.GetPackageRepository(ctx)
		return b.packages, err
	}
	blobsManifest := filepath.Join(packagesDir, "all_blobs.json")
	blobsData, err := ioutil.ReadFile(blobsManifest)
	if err != nil {
		if os.IsNotExist(err) {
			logger.Infof(ctx, "blobs manifest doesn't exist for build %d yet. Using archives.", b.id)
			b.packages, err = b.backupArchiveBuild.GetPackageRepository(ctx)
			return b.packages, err
		}
		return nil, fmt.Errorf("failed to read blobs manifest: %w", err)
	}

	var blobs []blob
	err = json.Unmarshal(blobsData, &blobs)
	if err != nil {
		return nil, fmt.Errorf("failed to unmarshal blobs JSON: %w", err)
	}
	var blobsList []string
	for _, b := range blobs {
		blobsList = append(blobsList, filepath.Join("blobs", b.Merkle))
	}
	logger.Infof(ctx, "all_blobs contains %d blobs", len(blobsList))

	p, err := packages.NewRepository(ctx, packagesDir)
	if err != nil {
		return nil, err
	}
	b.packages = p

	repoDir := filepath.Join(packagesDir, "repository")
	if err := b.archive.download(ctx, b.id, true, repoDir, blobsList); err != nil {
		logger.Errorf(ctx, "failed to download blobs to %s: %w", repoDir, err)
	}

	return b.packages, nil
}

// GetBuildImages downloads the build images for a specific build id.
// Returns a path to the directory of the downloaded images or an error if it
// fails to download.
func (b *ArtifactsBuild) GetBuildImages(ctx context.Context) (string, error) {
	if b.buildImageDir != "" {
		return b.buildImageDir, nil
	}
	artifact := "images"
	imageDir := filepath.Join(b.dir, b.id, artifact)
	if err := b.archive.download(ctx, b.id, false, imageDir, []string{artifact}); err != nil {
		logger.Infof(ctx, "failed to fetch artifacts for build %d. Using archives.", b.id)
		b.buildImageDir, err = b.backupArchiveBuild.GetBuildArchive(ctx)
		return b.buildImageDir, err
	}

	b.buildImageDir = imageDir

	return b.buildImageDir, nil
}

func (b *ArtifactsBuild) GetPaverDir(ctx context.Context) (string, error) {
	return b.GetBuildImages(ctx)
}

// GetPaver downloads and returns a paver for the build.
func (b *ArtifactsBuild) GetPaver(ctx context.Context) (paver.Paver, error) {
	return b.getPaver(ctx)
}

func (b *ArtifactsBuild) getPaver(ctx context.Context) (*paver.BuildPaver, error) {
	buildImageDir, err := b.GetBuildImages(ctx)
	if err != nil {
		return nil, err
	}

	currentBuildId := os.Getenv("BUILDBUCKET_ID")
	if currentBuildId == "" {
		currentBuildId = b.id
	}
	// Use the latest bootserver if possible because the one uploaded with the artifacts may not include bug fixes.
	bootserverPath := filepath.Join(buildImageDir, "bootserver")
	if err := b.archive.download(ctx, currentBuildId, false, bootserverPath, []string{"tools/linux-x64/bootserver"}); err != nil {
		return nil, fmt.Errorf("failed to download bootserver: %w", err)
	}
	// Make bootserver executable.
	if err := os.Chmod(bootserverPath, os.ModePerm); err != nil {
		return nil, fmt.Errorf("failed to make bootserver executable: %w", err)
	}

	return paver.NewBuildPaver(bootserverPath, buildImageDir, paver.SSHPublicKey(b.sshPublicKey))
}

func (b *ArtifactsBuild) GetSshPublicKey() ssh.PublicKey {
	return b.sshPublicKey
}

func (b *ArtifactsBuild) GetVbmetaPath(ctx context.Context) (string, error) {
	buildImageDir, err := b.GetBuildImages(ctx)
	if err != nil {
		return "", err
	}
	imagesJSON := filepath.Join(buildImageDir, paver.ImageManifest)
	f, err := os.Open(imagesJSON)
	if err != nil {
		return "", fmt.Errorf("failed to open %q: %w", imagesJSON, err)
	}
	defer f.Close()

	var items []struct {
		Name string `json:"name"`
		Path string `json:"path"`
		Type string `json:"type"`
	}
	if err := json.NewDecoder(f).Decode(&items); err != nil {
		return "", fmt.Errorf("failed to parse %q: %w", imagesJSON, err)
	}

	for _, item := range items {
		if item.Name == "zircon-a" && item.Type == "vbmeta" {
			return filepath.Join(buildImageDir, item.Path), nil
		}
	}

	return "", fmt.Errorf("failed to file zircon-a vbmeta in %q", imagesJSON)
}

func (b *ArtifactsBuild) Pave(ctx context.Context, deviceName string) error {
	paver, err := b.GetPaver(ctx)
	if err != nil {
		return err
	}

	return paver.Pave(ctx, deviceName)
}

func (b *ArtifactsBuild) String() string {
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

func (b *FuchsiaDirBuild) GetBootserver(ctx context.Context) (string, error) {
	return filepath.Join(b.dir, "host_x64/bootserver_new"), nil
}

func (b *FuchsiaDirBuild) GetPackageRepository(ctx context.Context) (*packages.Repository, error) {
	return packages.NewRepository(ctx, filepath.Join(b.dir, "amber-files"))
}

func (b *FuchsiaDirBuild) GetPaverDir(ctx context.Context) (string, error) {
	return b.dir, nil
}

func (b *FuchsiaDirBuild) GetPaver(ctx context.Context) (paver.Paver, error) {
	return paver.NewBuildPaver(
		filepath.Join(b.dir, "host_x64/bootserver_new"),
		b.dir,
		paver.SSHPublicKey(b.sshPublicKey),
	)
}

func (b *FuchsiaDirBuild) GetSshPublicKey() ssh.PublicKey {
	return b.sshPublicKey
}

func (b *FuchsiaDirBuild) GetVbmetaPath(ctx context.Context) (string, error) {
	imagesJSON := filepath.Join(b.dir, paver.ImageManifest)
	f, err := os.Open(imagesJSON)
	if err != nil {
		return "", fmt.Errorf("failed to open %q: %w", imagesJSON, err)
	}
	defer f.Close()

	var items []struct {
		Name string `json:"name"`
		Path string `json:"path"`
		Type string `json:"type"`
	}
	if err := json.NewDecoder(f).Decode(&items); err != nil {
		return "", fmt.Errorf("failed to parse %q: %w", imagesJSON, err)
	}

	for _, item := range items {
		if item.Name == "zircon-a" && item.Type == "vbmeta" {
			return filepath.Join(b.dir, item.Path), nil
		}
	}

	return "", fmt.Errorf("failed to file zircon-a vbmeta in %q", imagesJSON)
}

type OmahaBuild struct {
	build    Build
	omahaUrl string
	avbtool  *avb.AVBTool
	zbitool  *zbi.ZBITool
}

func NewOmahaBuild(build Build, omahaUrl string, avbtool *avb.AVBTool, zbitool *zbi.ZBITool) *OmahaBuild {
	return &OmahaBuild{build: build, omahaUrl: omahaUrl, avbtool: avbtool, zbitool: zbitool}
}

func (b *OmahaBuild) GetBootserver(ctx context.Context) (string, error) {
	return b.build.GetBootserver(ctx)
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
	bootserverPath, err := b.GetBootserver(ctx)
	if err != nil {
		return nil, err
	}

	// Create a ZBI with the omaha_url argument.
	tempDir, err := ioutil.TempDir("", "")
	if err != nil {
		return nil, fmt.Errorf("failed to create temp directory: %w", err)
	}
	defer os.RemoveAll(tempDir)

	// Create a ZBI with the omaha_url argument.
	destZbiPath := path.Join(tempDir, "omaha_argument.zbi")
	imageArguments := map[string]string{
		"omaha_url": b.omahaUrl,
	}

	if err := b.zbitool.MakeImageArgsZbi(ctx, destZbiPath, imageArguments); err != nil {
		return nil, fmt.Errorf("Failed to create ZBI: %w", err)
	}

	// Create a vbmeta that includes the ZBI we just created.
	propFiles := map[string]string{
		"zbi": destZbiPath,
	}

	destVbmetaPath := filepath.Join(paverDir, "zircon-a-omaha-test.vbmeta")

	srcVbmetaPath, err := b.GetVbmetaPath(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to find zircon-a vbmeta: %w", err)
	}

	err = b.avbtool.MakeVBMetaImage(ctx, destVbmetaPath, srcVbmetaPath, propFiles)
	if err != nil {
		return nil, fmt.Errorf("failed to create vbmeta: %w", err)
	}

	return paver.NewBuildPaver(
		bootserverPath,
		paverDir,
		paver.SSHPublicKey(b.GetSshPublicKey()),
		paver.OverrideVBMetaA(destVbmetaPath),
	)
}

func (b *OmahaBuild) GetSshPublicKey() ssh.PublicKey {
	return b.build.GetSshPublicKey()
}

func (b *OmahaBuild) GetVbmetaPath(ctx context.Context) (string, error) {
	return b.build.GetVbmetaPath(ctx)
}
