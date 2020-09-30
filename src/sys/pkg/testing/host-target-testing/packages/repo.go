// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package packages

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type Repository struct {
	Dir string
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
func NewRepository(ctx context.Context, dir string) (*Repository, error) {
	logger.Infof(ctx, "creating a repository for %q", dir)

	// The repository may have out of date metadata. This updates the repository to
	// the latest version so TUF won't complain about the data being old.
	repo, err := repo.New(dir)
	if err != nil {
		return nil, err
	}
	// Add an empty list of packages, which should update the targets
	// expiration date.
	if err := repo.AddTargets([]string{}, nil); err != nil {
		return nil, err
	}
	// Commit the update, which should update the snapshot and timestamp
	// expiration date.
	if err := repo.CommitUpdates(true); err != nil {
		return nil, err
	}

	return &Repository{
		Dir: filepath.Join(dir, "repository"),
	}, nil
}

// NewRepositoryFromTar extracts a repository from a tar.gz, and returns a
// Repository parsed from it. It returns an error if the repository does not
// exist, or contains malformed metadata.
func NewRepositoryFromTar(ctx context.Context, dst string, src string) (*Repository, error) {
	if err := util.Untar(ctx, dst, src); err != nil {
		return nil, fmt.Errorf("failed to extract packages: %w", err)
	}

	return NewRepository(ctx, filepath.Join(dst, "amber-files"))
}

// OpenPackage opens a package from the repository.
func (r *Repository) OpenPackage(path string) (Package, error) {
	// Parse the targets file so we can access packages locally.
	f, err := os.Open(filepath.Join(r.Dir, "targets.json"))
	if err != nil {
		return Package{}, err
	}
	defer f.Close()

	var s signed
	if err = json.NewDecoder(f).Decode(&s); err != nil {
		return Package{}, err
	}

	if target, ok := s.Signed.Targets[path]; ok {
		return newPackage(r, target.Custom.Merkle)
	}

	return Package{}, fmt.Errorf("could not find package: %q", path)

}

func (r *Repository) OpenBlob(merkle string) (*os.File, error) {
	return os.Open(filepath.Join(r.Dir, "blobs", merkle))
}

func (r *Repository) Serve(ctx context.Context, localHostname string, repoName string) (*Server, error) {
	return newServer(ctx, r.Dir, localHostname, repoName)
}

func (r *Repository) LookupUpdateSystemImageMerkle() (string, error) {
	return r.lookupUpdateContentPackageMerkle("update/0", "system_image/0")
}

func (r *Repository) LookupUpdatePrimeSystemImageMerkle() (string, error) {
	return r.lookupUpdateContentPackageMerkle("update_prime/0", "system_image_prime/0")
}

func (r *Repository) VerifyMatchesAnyUpdateSystemImageMerkle(merkle string) error {
	systemImageMerkle, err := r.LookupUpdateSystemImageMerkle()
	if err != nil {
		return err
	}
	if merkle == systemImageMerkle {
		return nil
	}

	systemPrimeImageMerkle, err := r.LookupUpdatePrimeSystemImageMerkle()
	if err != nil {
		return err
	}
	if merkle == systemPrimeImageMerkle {
		return nil
	}

	return fmt.Errorf("expected device to be running a system image of %s or %s, got %s",
		systemImageMerkle, systemPrimeImageMerkle, merkle)
}

func (r *Repository) lookupUpdateContentPackageMerkle(updatePackageName string, contentPackageName string) (string, error) {
	// Extract the "packages" file from the "update" package.
	p, err := r.OpenPackage(updatePackageName)
	if err != nil {
		return "", err
	}
	f, err := p.Open("packages.json")
	if err != nil {
		return "", err
	}

	packages, err := util.ParsePackagesJSON(f)
	if err != nil {
		return "", err
	}

	merkle, ok := packages[contentPackageName]
	if !ok {
		return "", fmt.Errorf("could not find %s merkle", contentPackageName)
	}

	return merkle, nil
}
