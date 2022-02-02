// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gcsutil

import (
	"context"
	"errors"
	"testing"

	"cloud.google.com/go/storage"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"
)

func TestRetry(t *testing.T) {
	ctx := context.Background()

	maxAttempts := 10 // arbitrary
	strategy := retry.WithMaxAttempts(retry.NewConstantBackoff(0), uint64(maxAttempts))

	tests := []struct {
		name           string
		err            error
		expectErr      bool
		expectAttempts int
	}{
		{
			name:           "pass",
			err:            nil,
			expectErr:      false,
			expectAttempts: 1,
		},
		{
			name:           "transient error",
			err:            errors.New("something transient"),
			expectErr:      true,
			expectAttempts: maxAttempts,
		},
		{
			name:           "nonexistent bucket",
			err:            storage.ErrBucketNotExist,
			expectErr:      true,
			expectAttempts: 1,
		},
		{
			name:           "nonexistent object",
			err:            storage.ErrObjectNotExist,
			expectErr:      true,
			expectAttempts: 1,
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			var attempts int
			err := retryWithStrategy(ctx, strategy, func() error {
				attempts++
				return test.err
			})
			if err != nil && !test.expectErr {
				t.Errorf("Unexpected error from Retry(): %s", err)
			} else if err == nil && test.expectErr {
				t.Errorf("Expected Retry to return an error, but got nil")
			}
			if attempts != test.expectAttempts {
				t.Errorf("Got %d attempts, expected %d", attempts, test.expectAttempts)
			}
		})
	}
}
