// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"

	"fuchsia.googlesource.com/tools/elflib"
)

// idsSource is a BinaryFileSource parsed from ids.txt
type IDsTxtRepo struct {
	lock      sync.RWMutex
	cached    map[string]elflib.BinaryFileRef
	pathToIDs string
	rel       bool
}

func NewIDsTxtRepo(pathToIDs string, rel bool) *IDsTxtRepo {
	return &IDsTxtRepo{
		cached:    make(map[string]elflib.BinaryFileRef),
		pathToIDs: pathToIDs,
		rel:       rel,
	}
}

func (i *IDsTxtRepo) getBinaries() ([]elflib.BinaryFileRef, error) {
	file, err := os.Open(i.pathToIDs)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	out, err := elflib.ReadIDsFile(file)
	if err != nil {
		return nil, err
	}
	if i.rel {
		base := filepath.Dir(i.pathToIDs)
		for idx, ref := range out {
			if !filepath.IsAbs(ref.Filepath) {
				out[idx].Filepath = filepath.Join(base, ref.Filepath)
			}
		}
	}
	return out, nil
}

func (i *IDsTxtRepo) readFromCache(buildID string) (elflib.BinaryFileRef, bool) {
	i.lock.RLock()
	defer i.lock.RUnlock()
	info, ok := i.cached[buildID]
	return info, ok
}

func (i *IDsTxtRepo) updateCache() error {
	i.lock.Lock()
	defer i.lock.Unlock()
	bins, err := i.getBinaries()
	if err != nil {
		return err
	}
	newCache := make(map[string]elflib.BinaryFileRef)
	// TODO(jakehehrlich): Do this in parallel.
	for _, bin := range bins {
		newCache[bin.BuildID] = bin
	}
	i.cached = newCache
	return nil
}

func (i *IDsTxtRepo) GetBuildObject(buildID string) (string, error) {
	if file, ok := i.readFromCache(buildID); ok && file.Verify() != nil {
		return file.Filepath, nil
	}
	if err := i.updateCache(); err != nil {
		return "", err
	}
	if file, ok := i.readFromCache(buildID); ok {
		if err := file.Verify(); err != nil {
			return "", err
		}
		return file.Filepath, nil
	}
	return "", fmt.Errorf("could not find file for %s", buildID)
}

type CompositeRepo struct {
	repos []Repository
}

// AddRepo adds a repo to be checked that has lower priority than any other
// previouslly added repo. This operation is not thread safe.
func (c *CompositeRepo) AddRepo(repo Repository) {
	c.repos = append(c.repos, repo)
}

func (c *CompositeRepo) GetBuildObject(buildID string) (string, error) {
	for _, repo := range c.repos {
		if file, err := repo.GetBuildObject(buildID); err != nil {
			return file, nil
		}
	}
	return "", fmt.Errorf("could not find file for %s", buildID)
}

type NewBuildIDRepo string

func (b NewBuildIDRepo) GetBuildObject(buildID string) (string, error) {
	if len(buildID) < 4 {
		return "", fmt.Errorf("build ID must be the hex representation of at least 2 bytes")
	}
	bin := elflib.BinaryFileRef{
		Filepath: filepath.Join(string(b), buildID[:2], buildID[2:]) + ".debug",
		BuildID:  buildID,
	}
	if err := bin.Verify(); err != nil {
		return "", err
	}
	return bin.Filepath, nil
}
