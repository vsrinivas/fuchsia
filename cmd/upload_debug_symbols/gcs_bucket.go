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
	"google.golang.org/api/iterator"
)

// GCSBucket provides access to a cloud storage bucket. GCSBucket only captures the items
// that were in GCS at the time the GCSBucket was created. It also tracks any items that
// are uploaded via upload().
type GCSBucket struct {
	objects map[string]bool
	bkt     *storage.BucketHandle
}

// previouslyContained returns true iff the given object is known to already exist in this
// bucket. GCSBucket only captures initial state; The object may already exist in the
// bucket when this method is invoked if some other process uploaded the object after this
// GCSBucket was created.
func (bkt *GCSBucket) previouslyContained(object string) bool {
	return bkt.objects[object]
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
	bkt.objects[object] = true
	return nil
}

// prepareGCSBucket captures the current state of the GCS bucket with the given name.
func prepareGCSBucket(ctx context.Context, name string) (*GCSBucket, error) {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to create client: %v", err)
	}
	bkt := client.Bucket(gcsBucket)
	if _, err = bkt.Attrs(ctx); err != nil {
		return nil, fmt.Errorf("failed to fetch bucket attributes: %v", err)
	}
	existingObjects := make(map[string]bool)
	it := bkt.Objects(ctx, nil)
	for {
		objAttrs, err := it.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			return nil, err
		}
		existingObjects[objAttrs.Name] = true
	}

	return &GCSBucket{
		objects: existingObjects,
		bkt:     bkt,
	}, nil
}
