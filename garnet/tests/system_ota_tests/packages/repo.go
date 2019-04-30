// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/repo"
)

type Repository struct {
	Dir     string
	targets targets
}

type signed struct {
	Signed targets `json:"signed"`
}

type targets struct {
	Targets map[string]targetFile `json:"targets"`
}

type targetFile struct {
	Custom custom `json:"custom"`
}

type custom struct {
	Merkle string `json:"merkle"`
}

// NewRepository parses the repository from the specified directory. It returns
// an error if the repository does not exist, or it contains malformed metadata.
func NewRepository(dir string) (*Repository, error) {
	// The repository may have out of date metadata. This updates the repository to
	// the latest version so TUF won't complain about the data being old.
	repo, err := repo.New(dir)
	if err != nil {
		return nil, err
	}
	if err := repo.CommitUpdates(true); err != nil {
		return nil, err
	}

	repoDir := filepath.Join(dir, "repository")

	// Parse the targets file so we can access packages locally.
	f, err := os.Open(filepath.Join(repoDir, "targets.json"))
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var s signed
	if err = json.NewDecoder(f).Decode(&s); err != nil {
		return nil, err
	}

	return &Repository{
		Dir:     repoDir,
		targets: s.Signed,
	}, nil
}

// Open a package from the p
func (r *Repository) OpenPackage(path string) (Package, error) {
	target, ok := r.targets.Targets[path]
	if !ok {
		return Package{}, fmt.Errorf("could not find package: %q", path)
	}

	return newPackage(r, target.Custom.Merkle)
}

func (r *Repository) OpenBlob(merkle string) (*os.File, error) {
	return os.Open(filepath.Join(r.Dir, "blobs", merkle))
}

func (r *Repository) Serve(localHostname string) (*Server, error) {
	return newServer(r.Dir, localHostname)
}
