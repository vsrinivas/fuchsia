// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/pkg"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type PackageBuilder struct {
	Name       string
	Repository string
	Version    string
	Cache      string
	Contents   map[string]string
}

func parsePackageJSON(path string) (string, string, error) {
	jsonData, err := ioutil.ReadFile(path)
	if err != nil {
		return "", "", fmt.Errorf("failed to read file at %s: %w", path, err)
	}
	var packageInfo pkg.Package
	if err := json.Unmarshal(jsonData, &packageInfo); err != nil {
		return "", "", fmt.Errorf("failed to unmarshal json data: %w", err)
	}
	return packageInfo.Name, packageInfo.Version, nil
}

// NewPackageBuilder returns a PackageBuilder
// Must call `Close()` to clean up PackageBuilder
func NewPackageBuilder(name string, version string, repository string) (*PackageBuilder, error) {
	if name == "" || version == "" {
		return nil, fmt.Errorf("missing package info and version information")
	}

	// Create temporary directory to store any additions that come in.
	tempDir, err := ioutil.TempDir("", "pm-temp-resource")
	if err != nil {
		return nil, fmt.Errorf("failed to create temp directory: %w", err)
	}

	return &PackageBuilder{
		Name:       name,
		Repository: repository,
		Version:    version,
		Cache:      tempDir,
		Contents:   make(map[string]string),
	}, nil
}

// NewPackageBuilderFromDir returns a PackageBuilder that initializes from the `dir` package directory.
// Must call `Close()` to clean up PackageBuilder
func NewPackageBuilderFromDir(dir string, name string, version string, repository string) (*PackageBuilder, error) {
	pkg, err := NewPackageBuilder(name, version, repository)
	if err != nil {
		return nil, err
	}

	err = filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return fmt.Errorf("walk of %s failed: %w", dir, err)
		}
		if !info.IsDir() {
			relativePath := strings.Replace(path, dir+"/", "", 1)
			pkg.Contents[relativePath] = path
		}
		return nil
	})
	if err != nil {
		return nil, fmt.Errorf("error when walking the directory: %w", err)
	}

	return pkg, nil
}

// Close removes temporary directories created by PackageBuilder.
func (p *PackageBuilder) Close() {
	os.RemoveAll(p.Cache)
}

// AddResource adds a resource to the package at the given path.
func (p *PackageBuilder) AddResource(path string, contents io.Reader) error {
	if _, ok := p.Contents[path]; ok {
		return fmt.Errorf("a resource already exists at path %q", path)
	}
	data, err := ioutil.ReadAll(contents)
	if err != nil {
		return fmt.Errorf("failed to read file: %w", err)
	}
	tempPath := filepath.Join(p.Cache, path)
	if err := os.MkdirAll(filepath.Dir(tempPath), os.ModePerm); err != nil {
		return fmt.Errorf("failed to create parent directories for %q: %w", tempPath, err)
	}
	if err = ioutil.WriteFile(tempPath, data, 0644); err != nil {
		return fmt.Errorf("failed to write data to %q: %w", tempPath, err)
	}
	p.Contents[path] = tempPath
	return nil
}

func tempConfig(dir string, name string, version string, repository string) (*build.Config, error) {
	cfg := &build.Config{
		OutputDir:     filepath.Join(dir, "output"),
		ManifestPath:  filepath.Join(dir, "manifest"),
		KeyPath:       filepath.Join(dir, "key"),
		TempDir:       filepath.Join(dir, "tmp"),
		PkgName:       name,
		PkgVersion:    version,
		PkgRepository: repository,
	}

	for _, d := range []string{cfg.OutputDir, cfg.TempDir} {
		if err := os.MkdirAll(d, os.ModePerm); err != nil {
			return nil, err
		}
	}

	return cfg, nil
}

// Publish the package to the repository. Returns the TUF package path and
// merkle on success, or a error on failure.
func (p *PackageBuilder) Publish(ctx context.Context, pkgRepo *Repository) (string, string, error) {
	// Open repository
	// Repository.Dir contains a trailing `repository` in the path that we don't want.
	repoDir := path.Dir(pkgRepo.Dir)
	pmRepo, err := repo.New(repoDir, pkgRepo.BlobStore.Dir())
	if err != nil {
		return "", "", fmt.Errorf("failed to open repository at %q. %w", pkgRepo.Dir, err)
	}
	// Create Config.
	dir, err := ioutil.TempDir("", "pm-temp-config")
	if err != nil {
		return "", "", fmt.Errorf("failed to create temp directory for the config: %w", err)
	}
	defer os.RemoveAll(dir)

	cfg, err := tempConfig(dir, p.Name, p.Version, p.Repository)
	if err != nil {
		return "", "", fmt.Errorf("failed to create temp config to fill with our data: %w", err)
	}

	pkgManifestPath := filepath.Join(filepath.Dir(cfg.ManifestPath), "package")
	if err := os.MkdirAll(filepath.Join(pkgManifestPath, "meta"), os.ModePerm); err != nil {
		return "", "", fmt.Errorf("failed to make parent dirs for meta/package: %w", err)
	}
	mfst, err := os.Create(cfg.ManifestPath)
	if err != nil {
		return "", "", fmt.Errorf("failed to create package manifest path: %w", err)
	}
	defer mfst.Close()

	// Fill config with our contents.
	for relativePath, sourcePath := range p.Contents {
		if relativePath == "meta/contents" {
			continue
		}
		if _, err := fmt.Fprintf(mfst, "%s=%s\n", relativePath, sourcePath); err != nil {
			return "", "", fmt.Errorf("failed to record entry %q as %q into manifest: %w", p.Name, sourcePath, err)
		}
	}

	// Save changes to config.
	if err := build.Update(cfg); err != nil {
		return "", "", fmt.Errorf("failed to update config: %w", err)
	}
	if _, err := build.Seal(cfg); err != nil {
		return "", "", fmt.Errorf("failed to seal config: %w", err)
	}

	pkgPath := fmt.Sprintf("%s/%s", p.Name, p.Version)
	blobs, err := cfg.BlobInfo()
	if err != nil {
		return "", "", fmt.Errorf("failed to extract blobs: %w", err)
	}

	pkgMerkle := ""
	for _, blob := range blobs {
		if blob.Path == "meta/" {
			pkgMerkle = blob.Merkle.String()
			break
		}
	}

	if pkgMerkle == "" {
		return "", "", fmt.Errorf("could not find meta.far merkle")
	}

	logger.Infof(ctx, "publishing %q to merkle %q", pkgPath, pkgMerkle)

	outputManifest, err := cfg.OutputManifest()
	if err != nil {
		return "", "", fmt.Errorf("failed to output manifest: %w", err)
	}

	content, err := json.Marshal(outputManifest)
	if err != nil {
		return "", "", fmt.Errorf("failed to convert manifest to JSON: %w", err)
	}

	outputManifestPath := path.Join(cfg.OutputDir, "package_manifest.json")
	if err := ioutil.WriteFile(outputManifestPath, content, os.ModePerm); err != nil {
		return "", "", fmt.Errorf("failed to write manifest JSON to %q: %w", outputManifestPath, err)
	}

	// Publish new config to repo.
	_, err = pmRepo.PublishManifest(outputManifestPath)
	if err != nil {
		return "", "", fmt.Errorf("failed to publish manifest: %w", err)
	}

	if err = pmRepo.CommitUpdates(true); err != nil {
		return "", "", fmt.Errorf("failed to commit updates to repo: %w", err)
	}

	logger.Infof(ctx, "package %q as %q published and committed", pkgPath, pkgMerkle)

	return pkgPath, pkgMerkle, nil
}
