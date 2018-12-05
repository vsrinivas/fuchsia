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
	t.Run("error", func(t *testing.T) {
		var i int
		err := Retry(context.Background(), &ZeroBackoff{}, func() error {
			i++
			if i == tries {
				return nil
			}
			return fmt.Errorf("try %d", i)
		})

		if err != nil {
			t.Errorf("unexpected error: %v", err)
		}
		if i != tries {
			t.Errorf("invalid number of tries: %d", i)
		}
	})
	t.Run("cancel", func(t *testing.T) {
		var i int
		ctx, cancel := context.WithCancel(context.Background())
		err := Retry(ctx, &ZeroBackoff{}, func() error {
			i++
			if i == tries {
				cancel()
			}
			return fmt.Errorf("try %d", i)
		})

		if err == nil {
			t.Error("error is nil")
		}
		if err.Error() != "try 5" {
			t.Errorf("unexpected error: %v", err)
		}
		if i != tries {
			t.Errorf("invalid number of tries: %d", i)
		}
	})
}
