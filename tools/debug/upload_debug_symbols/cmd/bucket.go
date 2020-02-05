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
)

// Abstract bucket storage interface.
type bucket interface {
	objectExists(context.Context, string) (bool, error)
	upload(context.Context, string, io.Reader) error
}

// GCSBucket implements the Bucket interface using cloud storage.
type GCSBucket struct {
	bkt *storage.BucketHandle
}

func (bkt *GCSBucket) objectExists(ctx context.Context, object string) (bool, error) {
	obj := bkt.bkt.Object(object)
	_, err := obj.Attrs(ctx)
	if err == storage.ErrObjectNotExist {
		return false, nil
	} else if err != nil {
		return false, fmt.Errorf("object possibly exists, but is in unknown state: %v", err)
	}
	return true, nil
}

func (bkt *GCSBucket) upload(ctx context.Context, object string, r io.Reader) error {
	wc := bkt.bkt.Object(object).If(storage.Conditions{DoesNotExist: true}).NewWriter(ctx)
	if _, err := io.Copy(wc, r); err != nil {
		wc.Close()
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
func newGCSBucket(ctx context.Context, name string) (*GCSBucket, error) {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to create client: %v", err)
	}
	bkt := client.Bucket(gcsBucket)
	return &GCSBucket{
		bkt: bkt,
	}, nil
}
