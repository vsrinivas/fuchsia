// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"strings"

	"cloud.google.com/go/storage"
	"fuchsia.googlesource.com/tools/gcs"
	"go.chromium.org/luci/auth"
)

// Client provides access to the artifacts produced by Fuchsia CI tasks.
type Client struct {
	client *storage.Client
}

// NewClient creates a new Client.
func NewClient(ctx context.Context, opts auth.Options) (*Client, error) {
	opts.Scopes = append(opts.Scopes, storage.ScopeReadWrite)
	client, err := gcs.NewClient(ctx, opts)
	if err != nil {
		return nil, err
	}
	return &Client{client: client}, nil
}

// GetBuildDir returns the BuildDirectory for the given build. bucket is the GCS bucket.
// build is the BuildBucket build ID.
func (c *Client) GetBuildDir(bucket, build string) *BuildDirectory {
	bkt := c.client.Bucket(bucket)
	return &BuildDirectory{&directory{
		bucket: bkt,
		root:   strings.Join([]string{"builds", build}, "/"),
	}}
}
