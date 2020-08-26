// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package retry

import (
	"context"
	"fmt"
	"testing"
)

func TestRetry(t *testing.T) {
	const tries = 5

	t.Run("retries until function returns nil", func(t *testing.T) {
		var i int
		err := Retry(context.Background(), &ZeroBackoff{}, func() error {
			i++
			if i == tries {
				return nil
			}
			return fmt.Errorf("try %d", i)
		}, nil)

		if err != nil {
			t.Errorf("unexpected error: %v", err)
		}
		if i != tries {
			t.Errorf("got %d tries, wanted %d", i, tries)
		}
	})

	t.Run("stops retrying after context is canceled", func(t *testing.T) {
		var i int
		ctx, cancel := context.WithCancel(context.Background())
		err := Retry(ctx, &ZeroBackoff{}, func() error {
			i++
			if i == tries {
				cancel()
			}
			return fmt.Errorf("try %d", i)
		}, nil)

		if err == nil {
			t.Error("error is nil")
		}
		expectedErr := "try 5"
		if err.Error() != expectedErr {
			t.Errorf("got error: %v, wanted: %s", err, expectedErr)
		}
		if i != tries {
			t.Errorf("got %d tries, wanted %d", i, tries)
		}
	})
}
