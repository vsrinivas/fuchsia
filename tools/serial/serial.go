// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package serial provides support for serial connections.
package serial

import (
	"context"
	"fmt"
	"io"
	"net"
	"strings"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	defaultBaudRate = 115200

	// Printed to the serial console when ready to accept user input.
	consoleCursor = "$ "

	// Timeout for reads and writes from a SerialSocket.
	defaultSocketIOTimeout = 2 * time.Minute
)

var diagnosticCmds = []Command{
	{[]string{"k", "threadload"}, 200 * time.Millisecond}, // Turn on threadload
	{[]string{"k", "threadq"}, 5 * time.Second},           // Turn on threadq and wait 5 sec
	{[]string{"k", "cpu", "sev"}, 5 * time.Second},        // Send a SEV and wait 5 sec
	{[]string{"k", "threadload"}, 200 * time.Millisecond}, // Turn off threadload
	{[]string{"k", "threadq"}, 0},                         // Turn off threadq
	// Invoke the threads command twice to identify hanging processes.
	{[]string{"threads", "--all-processes"}, 5 * time.Minute},
	{[]string{"threads", "--all-processes"}, 5 * time.Minute},
}

type SerialSocket struct {
	net.Conn

	// It's only safe for one goroutine at a time to read or write from the
	// socket at a time.
	mu        sync.Mutex
	ioTimeout time.Duration
}

func (s *SerialSocket) Read(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Conn.SetReadDeadline(time.Now().Add(s.ioTimeout))
	return s.Conn.Read(p)
}

func (s *SerialSocket) Write(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Conn.SetWriteDeadline(time.Now().Add(s.ioTimeout))
	return s.Conn.Write(p)
}

// SetIOTimeout sets the timeout for a single Read() or Write().
func (s *SerialSocket) SetIOTimeout(timeout time.Duration) {
	s.ioTimeout = timeout
}

// Open opens a new serial port using defaults.
func Open(name string) (io.ReadWriteCloser, error) {
	return OpenWithOptions(name, defaultBaudRate)
}

// OpenWithOptions opens a new serial port with the given name and baud rate.
func OpenWithOptions(name string, baudRate int) (io.ReadWriteCloser, error) {
	return open(name, baudRate)
}

// Command contains the command to run over serial and the expected duration
// to wait for the command to complete.
type Command struct {
	Cmd           []string
	SleepDuration time.Duration
}

func asSerialCmd(cmd []string) string {
	// The UART kernel driver expects a command to be followed by \r\n.
	// Send a leading \r\n for all commands as there may be characters in the buffer already
	// that we need to clear first.
	return fmt.Sprintf("\r\n%s\r\n", strings.Join(cmd, " "))
}

// NewSocket opens a connection on the provided `socketPath`.
func NewSocket(ctx context.Context, socketPath string) (*SerialSocket, error) {
	return NewSocketWithIOTimeout(ctx, socketPath, defaultSocketIOTimeout)
}

// NewSocketWithIOTimeout opens a connection on the provided `socketPath` with
// the provided socket IO timeout for reads and writes.
func NewSocketWithIOTimeout(ctx context.Context, socketPath string, ioTimeout time.Duration) (*SerialSocket, error) {
	if socketPath == "" {
		return nil, fmt.Errorf("serialSocketPath not set")
	}
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("failed to open serial socket connection: %v", err)
	}
	if ioTimeout <= 0 {
		ioTimeout = defaultSocketIOTimeout
	}
	socket := &SerialSocket{Conn: conn, ioTimeout: ioTimeout}
	// Trigger a new cursor print by sending a newline. This may do nothing if the
	// system was not ready to process input, but in that case it will print a
	// new cursor anyways when it is ready to receive input.
	io.WriteString(socket, asSerialCmd([]string{}))
	// Look for the cursor, which should indicate that the console is ready for input.
	ctx, cancel := context.WithTimeout(ctx, 45*time.Second)
	defer cancel()
	m := iomisc.NewMatchingReader(socket, [][]byte{[]byte(consoleCursor)})
	if _, err = iomisc.ReadUntilMatch(ctx, m); err != nil {
		return nil, fmt.Errorf("failed to find cursor: %v", err)
	}
	return socket, nil
}

// RunCommands writes the provided commands to the serial socket.
func RunCommands(ctx context.Context, socket io.ReadWriteCloser, cmds []Command) error {
	for _, cmd := range cmds {
		logger.Debugf(ctx, "running over serial: %v", cmd.Cmd)

		if _, err := io.WriteString(socket, asSerialCmd(cmd.Cmd)); err != nil {
			return fmt.Errorf("failed to write to serial socket: %v", err)
		}

		if cmd.SleepDuration > 0 {
			logger.Debugf(ctx, "sleeping for %v", cmd.SleepDuration)
			time.Sleep(cmd.SleepDuration)
		}
	}
	return nil
}

// RunDiagnostics runs a series of diagnostic commands over serial.
func RunDiagnostics(ctx context.Context, socket io.ReadWriteCloser) error {
	logger.Debugf(ctx, "attempting to run diagnostics over serial")
	return RunCommands(ctx, socket, diagnosticCmds)
}
