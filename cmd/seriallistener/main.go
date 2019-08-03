// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// Program to watch for a specific string to appear in a socket and
// then exits with code 0.  As long as the file is being actively written
// to the tool will keep watching, but after ioTimeout of inactivity it
// will fail.

// To manually test, do something like the following.
//
// $ SOCKET=socket
// $ rm $SOCKET &> /dev/null || true
// $ go run main.go -socket $SOCKET -success-string FOO &
// $ nc -U $SOCKET -l
//
// Any lines typed into stdin of nc will be sent to the this program.  When
// the success string is sent, this program will exit, closing the socket,
// and causing nc to exit.  Likewise, both programs will exit when the
// timeout is reached.

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"net"
	"os"
	"strings"
	"time"

	"go.fuchsia.dev/tools/color"
	"go.fuchsia.dev/tools/logger"
	"go.fuchsia.dev/tools/retry"
)

var (
	ioTimeout     time.Duration
	socket        string
	successString string
	colors        color.EnableColor
	level         logger.LogLevel
)

const serialSocketVar = "FUCHSIA_SERIAL_SOCKET"

func init() {
	flag.DurationVar(&ioTimeout, "io-timeout", 30*time.Second,
		"amount of time to wait for new data (seconds)")

	defaultSocket := os.Getenv(serialSocketVar)
	flag.StringVar(&socket, "socket", defaultSocket,
		fmt.Sprintf("unix socket to watch (required if %s not set)",
			serialSocketVar))

	flag.StringVar(&successString, "success-string", "",
		"string which means the test passed")

	colors = color.ColorAuto
	flag.Var(&colors, "color",
		"use color in output, can be never, auto, always")

	level = logger.TraceLevel
	flag.Var(&level, "level",
		"output verbosity, can be fatal, error, warning, info, "+
			"debug or trace")
}

func max(x int, y int) int {
	if x < y {
		return y
	}
	return x
}

func Main(ctx context.Context) error {
	if socket == "" {
		flag.Usage()
		return errors.New("no socket specified")
	}
	logger.Infof(ctx, "socket: %s", socket)

	if successString == "" {
		flag.Usage()
		return errors.New("no success string specified")
	}
	logger.Infof(ctx, "success string: %#v", successString)

	logger.Infof(ctx, "io-timeout: %v", ioTimeout)

	// Connect to the socket, retry for up to a minute.
	var err error
	var conn net.Conn
	backoff := retry.WithMaxDuration(
		retry.NewConstantBackoff(time.Second), time.Minute)
	retry.Retry(ctx, backoff, func() error {
		conn, err = net.DialTimeout("unix", socket, 5*time.Second)
		return err
	}, nil)
	if err != nil {
		return err
	}
	defer conn.Close()
	logger.Infof(ctx, "connection successful")

	buf := make([]byte, 4096)
	currPage := ""

	// Repeatedly read from the socket, setting a timeout before every Read().
	for {
		conn.SetReadDeadline(time.Now().Add(ioTimeout))
		n, err := conn.Read(buf)
		if err != nil {
			return err
		}

		currPage = currPage[max(0, len(currPage)-len(successString)):]
		currPage += string(buf[0:n])

		logger.Infof(ctx, "currPage: %#v", currPage)
		if strings.Contains(currPage, successString) {
			logger.Infof(ctx, "success string found")
			return nil
		}
	}
}

func main() {
	flag.Parse()

	log := logger.NewLogger(level, color.NewColor(colors),
		os.Stdout, os.Stderr, "seriallistener ")
	ctx := logger.WithLogger(context.Background(), log)

	err := Main(ctx)
	if err != nil {
		logger.Fatalf(ctx, "%s", err)
	}
}
