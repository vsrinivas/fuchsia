// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package command

import (
	"context"
	"os"
	"os/signal"
)

// CancelOnSignals returns a Context that emits a Done event when any of the input signals
// are received, assuming those signals can be handled by the current process.
func CancelOnSignals(ctx context.Context, sigs ...os.Signal) context.Context {
	ctx, cancel := context.WithCancel(ctx)
	c := make(chan os.Signal, 1)
	signal.Notify(c, sigs...)
	go func() {
		defer signal.Stop(c)
		select {
		case <-ctx.Done():
			return
		case <-c:
			cancel()
		}
	}()
	return ctx
}
