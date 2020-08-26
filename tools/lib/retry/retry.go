// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package retry

import (
	"context"
	"time"
)

// Retry the operation using the provided back-off policy until it succeeds, or
// the context is cancelled. Any intermediate errors (but not the final error)
// returned by the function will be sent on the given channel, if the channel is
// non-nil.
func Retry(ctx context.Context, b Backoff, f func() error, c chan<- error) error {
	b.Reset()
	var timer *time.Timer
	for {
		err := f()
		if err == nil {
			return nil
		}

		next := b.Next()
		if next == Stop {
			return err
		}

		if timer == nil {
			timer = time.NewTimer(next)
			defer timer.Stop()
		} else {
			// Reset() is only safe to call after the timer has fired and its
			// channel has been drained. At this point in the loop we know that
			// the timer's channel was drained on the last iteration (otherwise
			// we would have exited the loop), so it's safe to call Reset().
			timer.Reset(next)
		}

		// If the context has already been canceled, exit immediately rather
		// than entering the select statement to ensure determinism when the
		// backoff is zero. If multiple channels in a select statement are ready
		// when entering the statement, Go will randomly choose one, which is
		// not the desired behavior here - we always want to stop once the
		// context gets canceled.
		if ctx.Err() != nil {
			return err
		}

		select {
		case <-ctx.Done():
			return err
		case <-timer.C:
			if c != nil {
				c <- err
			}
		}
	}
}
