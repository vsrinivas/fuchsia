// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"strings"

	"cloud.google.com/go/storage"
	"fuchsia.googlesource.com/tools/gcs"
	"go.chromium.org/luci/auth"
)

// GCSBucket provides access to a cloud storage bucket.
type GCSBucket struct {
	bkt *storage.BucketHandle
}

func (bkt *GCSBucket) upload(ctx context.Context, object string, r io.Reader) error {
	wc := bkt.bkt.Object(object).If(storage.Conditions{DoesNotExist: true}).NewWriter(ctx)
	if _, err := io.Copy(wc, r); err != nil {
		return fmt.Errorf("failed to write object %q: %v", object, err)
	}
	// Close completes the write operation and flushes any buffered data.
	if err := wc.Close(); err != nil {
		// Error 412 means the precondition of DoesNotExist doesn't match.
		// It is the expected behavior since we don't want to upload duplicated files.
		if !strings.Contains(err.Error(), "Error 412") {
			return fmt.Errorf("failed in close: %v", err)
		}
	}
	return nil
}

// newGCSBucket returns a new GCSBucket object for the given bucket name.
func newGCSBucket(ctx context.Context, name string, opts auth.Options) (*GCSBucket, error) {
	client, err := gcs.NewClient(ctx, opts)
	if err != nil {
		return nil, fmt.Errorf("failed to create client: %v", err)
	}
	bkt := client.Bucket(gcsBucket)
	return &GCSBucket{
		bkt: bkt,
	}, nil
}
