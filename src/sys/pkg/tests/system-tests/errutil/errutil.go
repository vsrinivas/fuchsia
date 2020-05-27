// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package errutil

import (
	"context"
	"errors"
	"net"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// HandleError printsd backtraces on the device for certain errors.
func HandleError(ctx context.Context, serialSocketPath string, err error) {
	if !shouldPrintThreads(err) {
		return
	}

	// We can't print the backtrace if we aren't connected to serial.
	if serialSocketPath == "" {
		logger.Warningf(ctx, "not to configured to serial, cannot dump process backtraces")
		return
	}

	serial, err := net.Dial("unix", serialSocketPath)
	if err != nil {
		logger.Errorf(ctx, "failed to connect to serial socket: %v", err)
		return
	}
	defer serial.Close()

	logger.Infof(ctx, "printing all process backtraces to serial")

	if _, err := serial.Write([]byte("threads --all-processes\n")); err != nil {
		logger.Errorf(ctx, "failed to execute `threads --all-processes`: %v", err)
	}

	// Sleep for a small period of time and dump the traces again so we can tell
	// which trace might be hung.
	time.Sleep(1 * time.Second)

	logger.Infof(ctx, "printing all process backtraces to serial")
	if _, err := serial.Write([]byte("threads --all-processes\n")); err != nil {
		logger.Errorf(ctx, "failed to execute `threads --all-processes`: %v", err)
	}
}

func shouldPrintThreads(err error) bool {
	return errors.Is(err, context.DeadlineExceeded) ||
		strings.Contains(err.Error(), "use of closed network connection")
}
