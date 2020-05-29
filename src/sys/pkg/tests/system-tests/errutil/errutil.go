// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package errutil

import (
	"context"
	"errors"
	"fmt"
	"net"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// HandleError printsd backtraces on the device for certain errors.
func HandleError(ctx context.Context, serialSocketPath string, err error) error {
	if !shouldPrintThreads(err) {
		return nil
	}

	// We can't print the backtrace if we aren't connected to serial.
	if serialSocketPath == "" {
		logger.Warningf(ctx, "not to configured to serial, cannot dump process backtraces")
		return nil
	}

	serial, err := net.Dial("unix", serialSocketPath)
	if err != nil {
		return fmt.Errorf("failed to connect to serial socket: %v", err)
	}
	defer serial.Close()

	logger.Infof(ctx, "printing all process backtraces to serial")

	// Print a marker before printing the threads to make it easier to tell
	// where the traces start.
	if err := run(ctx, serial, "echo '--- all process backtrace trace 1 ---' &\r\n"); err != nil {
		return err
	}

	// Add a newline before executing the command to get a fresh line. Also
	// run it in the background in case `threads` hangs.
	if err := run(ctx, serial, "threads --all-processes&\n"); err != nil {
		return err
	}

	// Sleep for a period of time since we can't tell when `threads` has
	// finished executing.
	time.Sleep(60 * time.Second)

	if err := run(ctx, serial, "echo '--- all process trace backtrace 2---' &\n"); err != nil {
		return err
	}

	// dump the traces again so we can tell which trace might be hung.
	logger.Infof(ctx, "printing all process backtraces to serial again")
	if err := run(ctx, serial, "threads --all-processes&\n"); err != nil {
		return err
	}

	if err := run(ctx, serial, "echo '------------------------------------' &\n"); err != nil {
		return err
	}

	time.Sleep(60 * time.Second)

	logger.Infof(ctx, "done waiting for backtraces to be printed")

	return nil
}

func run(ctx context.Context, serial net.Conn, cmd string) error {
	logger.Infof(ctx, "executing %q", cmd)

	// Add a newline before executing the command to get a fresh line. Also
	// run it in the background in case `threads` hangs.
	if _, err := serial.Write([]byte(cmd)); err != nil {
		return fmt.Errorf("failed to execute %q: %w", cmd, err)
	}

	return nil
}

func shouldPrintThreads(err error) bool {
	return errors.Is(err, context.DeadlineExceeded) ||
		strings.Contains(err.Error(), "use of closed network connection")
}
