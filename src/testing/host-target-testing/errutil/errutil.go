// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package errutil

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"runtime/pprof"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// HandleError printsd backtraces on the device for certain errors.
func HandleError(ctx context.Context, serialSocketPath string, err error) error {
	if !shouldPrintThreads(err) {
		return nil
	}

	printGoBacktrace(ctx)

	// We can't print the backtrace if we aren't connected to serial.
	if serialSocketPath == "" {
		logger.Warningf(ctx, "not to configured to serial, cannot run serial commands")
		return nil
	}

	serial, err := net.Dial("unix", serialSocketPath)
	if err != nil {
		return fmt.Errorf("failed to connect to serial socket: %v", err)
	}
	defer serial.Close()

	// Discard any of the output from the serial port.
	go io.Copy(ioutil.Discard, serial)

	if err := printSyslogs(ctx, serial); err != nil {
		return err
	}

	if err := printDeviceProcessBacktraces(ctx, serial); err != nil {
		return err
	}

	// dump the traces again so we can tell which trace might be hung.
	if err := printDeviceProcessBacktraces(ctx, serial); err != nil {
		return err
	}

	return nil
}

func printSyslogs(ctx context.Context, serial net.Conn) error {
	logger.Infof(ctx, "printing system logs to serial")

	// Print syslog. This will also emit a newline before
	// the command to get a fresh line, and print markers before and after
	// the backtrace to make it easier to distinguish in the logs.
	cmd := fmt.Sprintf("\n(echo '%s --- system logs ---' && /bin/log_listener --dump_logs yes && echo '------------------------------------') &\n", time.Now().Format(time.RFC3339Nano))

	err := run(ctx, serial, cmd)
	if err != nil {
		return err
	}

	// We don't know how long it'll take to dump all threads, so sleep for
	// a minute to give `threads` a chance to complete.
	time.Sleep(60 * time.Second)

	logger.Infof(ctx, "done waiting for backtraces to be printed")

	return nil
}

func printGoBacktrace(ctx context.Context) {
	logger.Infof(ctx, "printing go backtrace")

	var buf bytes.Buffer
	pprof.Lookup("goroutine").WriteTo(&buf, 1)

	s := bufio.NewScanner(&buf)
	for s.Scan() {
		logger.Infof(ctx, "%s", s.Text())
	}
}

func printDeviceProcessBacktraces(ctx context.Context, serial net.Conn) error {
	logger.Infof(ctx, "printing all process backtraces to serial")

	// Print all process backtraces. This will also emit a newline before
	// the command to get a fresh line, and print markers before and after
	// the backtrace to make it easier to distinguish in the logs.
	cmd := fmt.Sprintf("\n(echo '%s --- all process backtrace ---' && threads --all-processes && echo '------------------------------------') &\n", time.Now().Format(time.RFC3339Nano))
	err := run(ctx, serial, cmd)
	if err != nil {
		return err
	}

	// We don't know how long it'll take to dump all threads, so sleep for
	// a minute to give `threads` a chance to complete.
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
