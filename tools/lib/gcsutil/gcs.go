// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gcsutil

import (
	"context"
	"errors"
	"time"

	"cloud.google.com/go/storage"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"
)

// Errors can be wrapped as transient, so that the transient exit code
// will be returned.
type TransientError struct {
	err error
}

func (e TransientError) Error() string { return e.err.Error() }

func (e TransientError) Unwrap() error { return e.err }

// Retry wraps a function that makes a GCS API call, adding retries for failures
// that might be transient.
func Retry(ctx context.Context, f func() error) error {
	const (
		initialWait = time.Second
		backoff     = 2
		maxAttempts = 5
	)
	retryStrategy := retry.WithMaxAttempts(
		retry.NewExponentialBackoff(initialWait, 0, backoff),
		maxAttempts)
	return retryWithStrategy(ctx, retryStrategy, f)
}

// Extracted to allow dependency injection for testing.
func retryWithStrategy(ctx context.Context, strategy retry.Backoff, f func() error) error {
	return retry.Retry(ctx, strategy, func() error {
		if err := f(); err != nil {
			if errors.Is(err, storage.ErrBucketNotExist) || errors.Is(err, storage.ErrObjectNotExist) {
				return retry.Fatal(err)
			}
			return TransientError{err: err}
		}
		return nil
	}, nil)
}

// ObjectAttrs gets the attributes for the given object, with retries.
func ObjectAttrs(ctx context.Context, obj *storage.ObjectHandle) (*storage.ObjectAttrs, error) {
	var objAttrs *storage.ObjectAttrs
	err := Retry(ctx, func() error {
		var err error
		objAttrs, err = obj.Attrs(ctx)
		return err
	})
	return objAttrs, err
}

// NewObjectReader gets a reader for the given object, with retries.
func NewObjectReader(ctx context.Context, obj *storage.ObjectHandle) (*storage.Reader, error) {
	var reader *storage.Reader
	err := Retry(ctx, func() error {
		var err error
		reader, err = obj.NewReader(ctx)
		return err
	})
	return reader, err
}
