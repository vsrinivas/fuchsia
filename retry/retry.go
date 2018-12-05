// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package retry

import (
	"context"
	"time"
)

// Retry the operation using the provided back-off policy until it succeeds,
// or the context is cancelled.
func Retry(ctx context.Context, b Backoff, f func() error) error {
	var err error
	var next time.Duration

	b.Reset()
	for {
		if err = f(); err == nil {
			break
		}

		if next = b.Next(); next == Stop {
			return err
		}

		timer := time.NewTimer(next)

		select {
		case <-ctx.Done():
			timer.Stop()
			return err
		case <-timer.C:
		}
	}

	return err
}
