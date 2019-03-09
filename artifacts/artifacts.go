// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"io"
	"strings"

	"cloud.google.com/go/storage"

	"google.golang.org/api/iterator"
)

// ArtifactsClient provides access to Fuchsia build artifacts.
type ArtifactsClient struct {
	client *storage.Client
}

// NewClient creates a new ArtifactsClient.
func NewClient(ctx context.Context) (*ArtifactsClient, error) {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, err
	}
	return &ArtifactsClient{client: client}, nil
}

// List lists all objects in the artifact directory for the given build. bucket is the
// Cloud Storage bucket for the given build.
func (c *ArtifactsClient) List(ctx context.Context, bucket, build string) ([]string, error) {
	dir := c.openDir(ctx, bucket, build)
	return dir.list(ctx)
}

// Open returns a reader for an object in the artifact directory for the given build.
// bucket is the Cloud Storage bucket for the given build.
func (c *ArtifactsClient) Open(ctx context.Context, bucket, build, path string) (io.Reader, error) {
	dir := c.openDir(ctx, bucket, build)
	return dir.open(ctx, path)
}

// Create returns a storage.Writer for an object in the artifact directory. bucket is the
// Cloud Storage bucket for the given build. build is the string Buildbucket build ID.
// path is the object path relative to the root of the build artifact directory.
func (c *ArtifactsClient) Create(ctx context.Context, bucket, build, path string) *storage.Writer {
	dir := c.openDir(ctx, bucket, build)
	return dir.create(ctx, path)
}

// openDir returns a Handle to a build's artifact directory within some bucket.
func (c *ArtifactsClient) openDir(ctx context.Context, bucket, build string) *directory {
	handle := c.client.Bucket(bucket)
	return &directory{bucket: handle, build: build}
}

// directory is used to read from a Fuchsia build's Cloud Storage artifact "directory",
// which is expected to live in a bucket containing the following hierarchy, where
// "build-N" represents a possible directory:
//
// <bucket>/
//         "builds"/
//                  <build-1>/
//                  <build-2>/
//                  ...
type directory struct {
	bucket *storage.BucketHandle
	build  string
}

// open returns an io.Reader for the given object in this directory.
func (d *directory) open(ctx context.Context, object string) (io.Reader, error) {
	object = strings.Join([]string{"builds", d.build, object}, "/")
	return d.bucket.Object(object).NewReader(ctx)
}

// create returns a storage.Writer for the given object in this directory.
func (d *directory) create(ctx context.Context, object string) *storage.Writer {
	object = strings.Join([]string{"builds", d.build, object}, "/")
	return d.bucket.Object(object).NewWriter(ctx)
}

// List lists all of the objects in this directory.
func (d *directory) list(ctx context.Context) ([]string, error) {
	prefix := strings.Join([]string{"builds", d.build}, "/")
	iter := d.bucket.Objects(ctx, &storage.Query{
		Prefix: prefix,
	})

	var items []string
	for {
		attrs, err := iter.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			return nil, err
		}
		items = append(items, strings.TrimPrefix(attrs.Name, prefix+"/"))
	}

	return items, nil
}
