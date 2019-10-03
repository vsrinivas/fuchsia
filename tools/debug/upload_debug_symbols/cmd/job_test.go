// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
)

func TestJob(t *testing.T) {
	buildID := "foo"
	filename := fmt.Sprintf("%s.debug", buildID)
	bucket := "bucket"
	tmpFile, err := ioutil.TempFile("", filename)
	if err != nil {
		t.Fatalf("Failed to create tempfile: %v", err)
	}
	defer os.Remove(tmpFile.Name())
	binaryFileRef := elflib.NewBinaryFileRef(tmpFile.Name(), buildID)
	job := newJob(binaryFileRef, bucket)
	expectedGcsPath := fmt.Sprintf("gs://%s/%s", bucket, filename)
	expectedName := fmt.Sprintf("ensure %q in %s", buildID, expectedGcsPath)
	ctx := context.Background()
	t.Run("verify gcs path", func(t *testing.T) {
		if job.gcsPath != expectedGcsPath {
			t.Fatalf("Incorrect GCS path; expected %s, got %s", expectedGcsPath, job.gcsPath)
		}
	})
	t.Run("verify name", func(t *testing.T) {
		if job.name != expectedName {
			t.Fatalf("Incorrect job name; expected %s, got %s", job.name, expectedName)
		}
	})
	t.Run("ensure on nonexistent object", func(t *testing.T) {
		bkt := &mockBucket{contents: map[string]bool{"other.debug": true}}
		if err := job.ensure(ctx, bkt); err != nil {
			t.Fatalf("Expected nil error, got %v", err)
		}
	})
	t.Run("ensure on existing object", func(t *testing.T) {
		bkt := &mockBucket{contents: map[string]bool{filename: true}}
		if err := job.ensure(ctx, bkt); err != nil {
			t.Fatalf("Expected nil error, got %v", err)
		}
	})
	t.Run("ensure with upload error", func(t *testing.T) {
		bkt := &mockBucket{
			contents:  map[string]bool{"other.debug": true},
			uploadErr: fmt.Errorf("error during upload"),
		}
		if err := job.ensure(ctx, bkt); err == nil {
			t.Fatalf("Expected error, got nil")
		}
	})
	t.Run("ensure with unknown object state error", func(t *testing.T) {
		bkt := &mockBucket{
			contents:        map[string]bool{"other.debug": true},
			objectExistsErr: fmt.Errorf("unknown object state"),
		}
		if err := job.ensure(ctx, bkt); err == nil {
			t.Fatalf("Expected error, got %v", err)
		}
	})
}
