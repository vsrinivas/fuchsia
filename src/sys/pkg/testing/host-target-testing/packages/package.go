// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/garnet/go/src/far"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
)

type FileData []byte

type Package struct {
	merkle   string
	repo     *Repository
	contents build.MetaContents
}

// newPackage extracts out a package from the repository.
func newPackage(repo *Repository, merkle string) (Package, error) {
	// Need to parse out the package meta.far to find the package contents.
	blob, err := repo.OpenBlob(merkle)
	if err != nil {
		return Package{}, err
	}
	defer blob.Close()

	f, err := far.NewReader(blob)
	if err != nil {
		return Package{}, err
	}
	defer f.Close()

	b, err := f.ReadFile("meta/contents")
	if err != nil {
		return Package{}, err
	}

	contents, err := build.ParseMetaContents(bytes.NewReader(b))
	if err != nil {
		return Package{}, err
	}

	return Package{
		merkle:   merkle,
		repo:     repo,
		contents: contents,
	}, nil
}

// Merkle returns the meta.far merkle.
func (p *Package) Merkle() string {
	return p.merkle
}

// Open opens a file in the package.
func (p *Package) Open(path string) (*os.File, error) {
	merkle, ok := p.contents[path]
	if !ok {
		return nil, os.ErrNotExist
	}

	return p.repo.OpenBlob(merkle.String())
}

// ReadFile reads a file from a package.
func (p *Package) ReadFile(path string) ([]byte, error) {
	r, err := p.Open(path)
	if err != nil {
		return nil, err
	}

	return ioutil.ReadAll(r)
}

func (p *Package) Expand(dir string) error {
	for path := range p.contents {
		data, err := p.ReadFile(path)
		if err != nil {
			return fmt.Errorf("invalid path. %w", err)
		}
		realPath := filepath.Join(dir, path)
		if err := os.MkdirAll(filepath.Dir(realPath), 0755); err != nil {
			return fmt.Errorf("could not create parent directories for %s, %w", realPath, err)
		}
		if err = ioutil.WriteFile(realPath, data, 0644); err != nil {
			return fmt.Errorf("could not export %s to %s. %w", path, realPath, err)
		}
	}
	blob, err := p.repo.OpenBlob(p.merkle)
	if err != nil {
		return fmt.Errorf("failed to open meta.far blob. %w", err)
	}
	defer blob.Close()

	f, err := far.NewReader(blob)
	if err != nil {
		return fmt.Errorf("failed to open reader on blob. %w", err)
	}
	defer f.Close()

	for _, path := range f.List() {
		data, err := f.ReadFile(path)
		if err != nil {
			fmt.Errorf("failed to read %s. %w", path, err)
		}
		realPath := filepath.Join(dir, path)
		if err := os.MkdirAll(filepath.Dir(realPath), 0755); err != nil {
			return fmt.Errorf("could not create parent directories for %s, %w", realPath, err)
		}
		if err = ioutil.WriteFile(realPath, data, 0644); err != nil {
			fmt.Errorf("failed to write file %s. %w", realPath, err)
		}
	}

	return nil
}
