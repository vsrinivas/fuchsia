// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	"cloud.google.com/go/storage"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/lib/cache"
)

// FileCloser holds a reference to a file and prevents it from being deleted
// until after Close() is called. The filename can be retrieved via String()
type FileCloser interface {
	String() string
	Close() error
}

// Repository represents a collection of debug binaries referable to by
// filepath and indexed by their build ID.
type Repository interface {
	// GetBuildObject takes a build ID and returns the corresponding file
	// reference via a FileCloser.
	GetBuildObject(buildID string) (FileCloser, error)
}

type NopFileCloser string

func (d NopFileCloser) String() string {
	return string(d)
}

func (d NopFileCloser) Close() error {
	return nil
}

type buildIDKey string

func (b buildIDKey) Hash() string {
	return string(b)
}

// CloudRepo represents a repository stored in a GCS bucket.
type CloudRepo struct {
	client  *storage.Client
	bucket  *storage.BucketHandle
	cache   *cache.FileCache
	timeout *time.Duration
}

// NewCloudRepo creates a CloudRepo using bucketName. The connection to the bucket
// will be ended when ctx is canceled. No timeout on GetBuildObject is set until
// SetTimeout is called.
func NewCloudRepo(ctx context.Context, bucketName string, cache *cache.FileCache) (*CloudRepo, error) {
	var out CloudRepo
	var err error
	if out.client, err = storage.NewClient(ctx); err != nil {
		return nil, err
	}
	out.bucket = out.client.Bucket(bucketName)
	out.cache = cache
	return &out, nil
}

// SetTimeout sets the maximum duration that GetBuildObject will wait before
// canceling the download from GCS.
func (c *CloudRepo) SetTimeout(t time.Duration) {
	c.timeout = &t
}

// GetBuildObject checks the cache for the debug object. If available it uses that.
// Otherwise it downloads the object, adds it to the cache, and returns the local
// reference.
func (c *CloudRepo) GetBuildObject(buildID string) (FileCloser, error) {
	out, err := c.cache.Get(buildIDKey(buildID))
	if err == nil {
		return out, nil
	}
	obj := c.bucket.Object(buildID + ".debug")
	ctx := context.Background()
	if c.timeout != nil {
		var cancel func()
		ctx, cancel = context.WithTimeout(ctx, *c.timeout)
		defer cancel()
	}
	r, err := obj.NewReader(ctx)
	if err != nil {
		return nil, err
	}
	out, err = c.cache.Add(buildIDKey(buildID), r)
	if err != nil {
		return nil, err
	}
	return out, nil
}

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

func (i *IDsTxtRepo) GetBuildObject(buildID string) (FileCloser, error) {
	if file, ok := i.readFromCache(buildID); ok && file.Verify() != nil {
		return NopFileCloser(file.Filepath), nil
	}
	if err := i.updateCache(); err != nil {
		return nil, err
	}
	if file, ok := i.readFromCache(buildID); ok {
		if err := file.Verify(); err != nil {
			return nil, err
		}
		return NopFileCloser(file.Filepath), nil
	}
	return nil, fmt.Errorf("could not find file for %s", buildID)
}

type CompositeRepo struct {
	repos []Repository
}

// AddRepo adds a repo to be checked that has lower priority than any other
// previouslly added repo. This operation is not thread safe.
func (c *CompositeRepo) AddRepo(repo Repository) {
	c.repos = append(c.repos, repo)
}

func (c *CompositeRepo) GetBuildObject(buildID string) (FileCloser, error) {
	for _, repo := range c.repos {
		file, err := repo.GetBuildObject(buildID)
		if err != nil {
			continue
		}
		return file, nil
	}
	return nil, fmt.Errorf("could not find file for %s", buildID)
}

type NewBuildIDRepo string

func (b NewBuildIDRepo) GetBuildObject(buildID string) (FileCloser, error) {
	if len(buildID) < 4 {
		return nil, errors.New("build ID must be the hex representation of at least 2 bytes")
	}
	bin := elflib.BinaryFileRef{
		Filepath: filepath.Join(string(b), buildID[:2], buildID[2:]) + ".debug",
		BuildID:  buildID,
	}
	if err := bin.Verify(); err != nil {
		return nil, err
	}
	return NopFileCloser(bin.Filepath), nil
}
