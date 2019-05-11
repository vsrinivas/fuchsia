// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"bytes"
	"io/ioutil"
	"os"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/build"
)

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
