// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"log"
	"os"

	"fuchsia.googlesource.com/tools/elflib"
)

// job is a description of some BinaryFileRef to upload to GCS. The object's name in GCS
// is formed by concatenating the ref's BuildID with elflib.DebugFileSuffix.
type job struct {
	// The BinaryFileRef to upload to GCS.
	bfr elflib.BinaryFileRef

	// dstBucket is the destination GCS bucket.
	dstBucket string
}

// Returns a human-readable display name for this job.
func (j *job) name() string {
	return fmt.Sprintf("upload %q to gs://%s/%s", j.bfr.BuildID, j.dstBucket, j.bfr.BuildID)
}

func (j *job) execute(ctx context.Context, bkt *GCSBucket) error {
	if err := j.bfr.Verify(); err != nil {
		return fmt.Errorf("validation failed for %q: %v", j.bfr.Filepath, err)
	}
	object := j.bfr.BuildID + elflib.DebugFileSuffix
	bucket := j.dstBucket
	if bkt.previouslyContained(object) {
		log.Printf("skipping %q which already exists in %q", object, bucket)
		return nil
	}
	filepath := j.bfr.Filepath
	reader, err := os.Open(filepath)
	if err != nil {
		return fmt.Errorf("failed to open %q: %v", filepath, err)
	}
	return bkt.upload(ctx, object, reader)
}
