// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// Program to watch for a specific string to appear from a socket's output and
// then exits successfully.

import (
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	socketPathEnvVar = "FUCHSIA_SERIAL_SOCKET"
)

var (
	timeout        time.Duration
	successString  string
	redirectStdout bool
)

func init() {
	flag.DurationVar(&timeout, "timeout", 10*time.Minute, "amount of time to wait for success string")
	flag.BoolVar(&redirectStdout, "stdout", false, "whether to redirect serial output to stdout")
	flag.StringVar(&successString, "success-str", "", "string that - if read - indicates success")
}

func execute(ctx context.Context, socketPath string, stdout io.Writer) error {
	if socketPath == "" {
		flag.Usage()
		return fmt.Errorf("could not find socket in environment")
	}
	logger.Debugf(ctx, "socket: %s", socketPath)

	if successString == "" {
		flag.Usage()
		return fmt.Errorf("-success is a required argument")
	}

	socket, err := net.Dial("unix", socketPath)
	if err != nil {
		return err
	}
	defer socket.Close()

	m := iomisc.NewSequenceMatchingReader(socket, successString)
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	errs := make(chan error)
	go func() {
		if _, err := io.Copy(stdout, m); err != nil {
			errs <- err
		}
	}()

	go func() {
		for {
			if m.Match() != nil {
				logger.Debugf(ctx, "success string found: %q", successString)
				errs <- nil
				return
			}
		}
	}()

	select {
	case <-ctx.Done():
		return fmt.Errorf("timed out before success string %q was read from serial", successString)
	case err := <-errs:
		return err
	}
}

func main() {
	flag.Parse()

	log := logger.NewLogger(logger.DebugLevel, color.NewColor(color.ColorAuto),
		os.Stdout, os.Stderr, "seriallistener ")
	ctx := logger.WithLogger(context.Background(), log)

	socketPath := os.Getenv(socketPathEnvVar)

	stdout := ioutil.Discard
	if redirectStdout {
		stdout = os.Stdout
	}

	if err := execute(ctx, socketPath, stdout); err != nil {
		logger.Fatalf(ctx, "%s", err)
	}
}
