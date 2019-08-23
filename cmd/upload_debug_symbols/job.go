// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"os"

	"go.fuchsia.dev/tools/elflib"
)

// job is a description of some BinaryFileRef to upload to GCS. The object's name in GCS
// is formed by concatenating the ref's BuildID with elflib.DebugFileSuffix.
type job struct {
	// The BinaryFileRef to upload to GCS.
	bfr elflib.BinaryFileRef

	// dstBucket is the destination GCS bucket.
	dstBucket string

	// gcsPath is the derived destination GCS path.
	gcsPath string

	// name is a human-readable display name for this job.
	name string
}

func newJob(bfr elflib.BinaryFileRef, dstBucket string) job {
	gcsPath := gcsPath(bfr, dstBucket)
	name := name(bfr, gcsPath)
	return job{
		bfr:       bfr,
		dstBucket: dstBucket,
		gcsPath:   gcsPath,
		name:      name,
	}
}

// Returns the GCS path for a bfr upload.
func gcsPath(bfr elflib.BinaryFileRef, dstBucket string) string {
	return fmt.Sprintf("gs://%s/%s%s", dstBucket, bfr.BuildID, elflib.DebugFileSuffix)
}

// Returns a human-readable display name for a bfr upload.
func name(bfr elflib.BinaryFileRef, gcsPath string) string {
	return fmt.Sprintf("upload %q to %s", bfr.BuildID, gcsPath)
}

func (j *job) execute(ctx context.Context, bkt *GCSBucket) error {
	object := j.bfr.BuildID + elflib.DebugFileSuffix
	filepath := j.bfr.Filepath
	reader, err := os.Open(filepath)
	if err != nil {
		return fmt.Errorf("failed to open %q: %v", filepath, err)
	}
	return bkt.upload(ctx, object, reader)
}
