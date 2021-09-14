// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serial

import (
	"context"
	"fmt"
	"io"
	"math/rand"
	"net"
	"os"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"golang.org/x/sync/errgroup"
)

type fakeSerialServer struct {
	received       []byte
	shutdownString string
	socketPath     string
	listeningChan  chan bool
}

func (s *fakeSerialServer) Serve() error {
	listener, err := net.Listen("unix", s.socketPath)
	if err != nil {
		s.listeningChan <- false
		return fmt.Errorf("Listen(%s) failed: %v", s.socketPath, err)
	}
	defer listener.Close()
	s.listeningChan <- true
	conn, err := listener.Accept()
	if err != nil {
		return fmt.Errorf("Accept() failed: %v", err)
	}
	defer conn.Close()
	// Signal we're ready to accept input.
	if _, err := conn.Write([]byte(consoleCursor)); err != nil {
		return fmt.Errorf("conn.Write() failed: %v", err)
	}
	reader := iomisc.NewMatchingReader(conn, [][]byte{[]byte(s.shutdownString)})
	for {
		buf := make([]byte, 1024)
		bytesRead, err := reader.Read(buf)
		s.received = append(s.received, buf[:bytesRead]...)
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return fmt.Errorf("conn.Read() failed: %v", err)
		}
	}
}

func TestRunCommands(t *testing.T) {
	socketPath := fmt.Sprintf("%d.sock", rand.Uint32())
	defer os.Remove(socketPath)
	server := fakeSerialServer{
		shutdownString: "dm shutdown",
		socketPath:     socketPath,
		listeningChan:  make(chan bool),
	}
	eg := errgroup.Group{}
	eg.Go(server.Serve)

	if !<-server.listeningChan {
		t.Fatalf("fakeSerialServer.Serve() returned: %v", eg.Wait())
	}

	clientSocket, err := NewSocket(context.Background(), socketPath)
	if err != nil {
		t.Fatalf("NewSocket() failed: %v", err)
	}

	// Test that SetIOTimeout causes the socket to return from Read() even
	// if there is nothing to read.
	clientSocket.SetIOTimeout(time.Nanosecond)
	buf := make([]byte, 10)
	_, err = clientSocket.Read(buf)
	if err == nil {
		t.Errorf("expected Read() to get an error, got nil")
	}
	clientSocket.SetIOTimeout(defaultSocketIOTimeout)

	diagnosticCmds = []Command{
		{
			Cmd: []string{"foo"},
			// Ensure we don't waste time sleeping in this test.
			SleepDuration: time.Microsecond,
		},
		{
			Cmd: []string{"bar"},
			// Ensure we don't waste time sleeping in this test.
			SleepDuration: time.Microsecond,
		},
		{
			// This is a hack to ensure the shutdown command gets sent to the serial server.
			// Rather than introduce a new synchronization mechanism, just use the code under test's
			// existing mechanism for sending commands.
			Cmd: []string{server.shutdownString},
			// Ensure we don't waste time sleeping in this test.
			SleepDuration: time.Microsecond,
		},
	}
	// RunDiagnostics calls RunCommands so this covers both functions for testability.
	err = RunDiagnostics(context.Background(), clientSocket)
	if err != nil {
		t.Errorf("RunDiagnostics failed: %v", err)
	}
	if err = eg.Wait(); err != nil {
		t.Errorf("server returned: %v", err)
	}
	if err = clientSocket.Close(); err != nil {
		t.Errorf("clientSocket.Close() returned: %v", err)
	}
	// Verify that each command was seen in the received data, and in the
	// proper order. The first command run was an empty command in NewSocket().
	expectedCommands := asSerialCmd([]string{})
	for _, cmd := range diagnosticCmds {
		expectedCommands += asSerialCmd(cmd.Cmd)
	}
	if diff := cmp.Diff(expectedCommands, string(server.received)); diff != "" {
		t.Errorf("Unexpected server.received (-want +got):\n%s", diff)
	}
}
