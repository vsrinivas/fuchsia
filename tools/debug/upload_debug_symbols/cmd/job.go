// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"os"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
)

// job is a description of some BinaryFileRef to ensure existence in GCS.
// The object's name in GCS is formed by concatenating the ref's BuildID with
// elflib.DebugFileSuffix.
type job struct {
	// The BinaryFileRef to ensure existence in GCS.
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

// Returns the GCS path for a bfr ensure call.
func gcsPath(bfr elflib.BinaryFileRef, dstBucket string) string {
	return fmt.Sprintf("gs://%s/%s%s", dstBucket, bfr.BuildID, elflib.DebugFileSuffix)
}

// Returns a human-readable display name for a bfr ensure call.
func name(bfr elflib.BinaryFileRef, gcsPath string) string {
	return fmt.Sprintf("ensure %q in %s", bfr.BuildID, gcsPath)
}

func (j *job) ensure(ctx context.Context, bkt bucket) error {
	object := j.bfr.BuildID + elflib.DebugFileSuffix
	exists, err := bkt.objectExists(ctx, object)
	if err != nil {
		return fmt.Errorf("failed to determine object %s existence: %w", object, err)
	}
	if exists {
		return nil
	}
	filepath := j.bfr.Filepath
	reader, err := os.Open(filepath)
	if err != nil {
		return fmt.Errorf("failed to open %q: %w", filepath, err)
	}
	defer reader.Close()
	return bkt.upload(ctx, object, reader)
}
