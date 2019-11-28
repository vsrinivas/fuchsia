// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package schedule

import (
	"context"
	"time"
)

// OncePerTick calls the provided function `oncePerTick` every time a channel
// send occurs on `ticker`, unless a channel send has occurred on `eagerly`
// since the last send on `ticker`. `onNotify` is called every time a channel
// send occurs on `eagerly`.
func OncePerTick(ctx context.Context, eagerly <-chan struct{}, ticker <-chan time.Time, onNotify func(), oncePerTick func(time.Time)) error {
	var scheduledThisTick bool
	for {
		var ts time.Time
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-eagerly:
			onNotify()
			if scheduledThisTick {
				continue
			}
			scheduledThisTick = true
			ts = time.Now()
		case ts = <-ticker:
			scheduledLastTick := scheduledThisTick
			scheduledThisTick = false
			if scheduledLastTick {
				continue
			}
		}
		oncePerTick(ts)
	}
}
