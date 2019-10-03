// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"strings"
	"testing"
)

type mockBucket struct {
	contents        map[string]bool
	objectExistsErr error
	uploadErr       error
}

func (bkt *mockBucket) objectExists(ctx context.Context, object string) (bool, error) {
	if bkt.objectExistsErr != nil {
		return false, bkt.objectExistsErr
	}
	_, ok := bkt.contents[object]
	return ok, nil
}

func (bkt *mockBucket) upload(ctx context.Context, object string, r io.Reader) error {
	return bkt.uploadErr
}

func TestObjectExistence(t *testing.T) {
	newObject := "new.debug"
	existingObject := "exists.debug"
	bkt := &mockBucket{contents: map[string]bool{existingObject: true}}
	ctx := context.Background()
	t.Run("object does not exist", func(t *testing.T) {
		exists, err := bkt.objectExists(ctx, newObject)
		if err != nil {
			t.Fatalf("Expected nil error, got %v", err)
		}
		if exists {
			t.Fatalf("Object should not exist; expected %t, got %t", false, exists)
		}
	})
	t.Run("object exists", func(t *testing.T) {
		exists, err := bkt.objectExists(ctx, existingObject)
		if err != nil {
			t.Fatalf("Expected nil error; got %v", err)
		}
		if !exists {
			t.Fatalf("Object should exist; expected %t, got %t", true, exists)
		}
	})
	t.Run("unknown object state", func(t *testing.T) {
		errBkt := &mockBucket{
			contents:        make(map[string]bool),
			objectExistsErr: fmt.Errorf("unknown object state"),
		}
		_, err := errBkt.objectExists(ctx, existingObject)
		if err == nil {
			t.Fatalf("Expected error; got %v", err)
		}
	})
}

func TestUpload(t *testing.T) {
	object := "foo.debug"
	r := strings.NewReader("some bytes")
	ctx := context.Background()
	t.Run("test successful upload", func(t *testing.T) {
		bkt := &mockBucket{contents: map[string]bool{object: true}, uploadErr: nil}
		if err := bkt.upload(ctx, object, r); err != nil {
			t.Fatalf("Expected nil error, got %v", err)
		}
	})
	t.Run("test error on upload", func(t *testing.T) {
		bkt := &mockBucket{
			contents:  map[string]bool{object: true},
			uploadErr: fmt.Errorf("some error"),
		}
		if err := bkt.upload(ctx, object, r); err == nil {
			t.Fatalf("Expected error, got nil")
		}
	})
}
