// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"bytes"
	"context"
	"errors"
	"io"
	"net"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"

	"golang.org/x/crypto/ssh"
)

const testTimeout = 1 * time.Second

func setUpConn(
	ctx context.Context,
	t *testing.T,
	onNewChannel func(ssh.NewChannel),
	onRequest func(*ssh.Request),
) (*Conn, *sshServer) {
	server, err := startSSHServer(onNewChannel, onRequest)
	if err != nil {
		t.Fatalf("failed to start ssh server: %v", err)
	}
	t.Cleanup(server.stop)

	conn, err := connect(ctx, server.addr, server.clientConfig, retry.NoRetries())
	if err != nil {
		t.Fatalf("failed to create conn: %v", err)
	}
	t.Cleanup(conn.Close)

	return conn, server
}

func assertChannelClosed(t *testing.T, ch chan struct{}, errorMessage string) {
	select {
	case <-ch:
	case <-time.After(testTimeout):
		t.Errorf(errorMessage)
	}
}

func TestKeepalive(t *testing.T) {
	ctx := context.Background()

	t.Run("sends pings when timer fires", func(t *testing.T) {
		requestsReceived := make(chan *ssh.Request, 1)

		conn, _ := setUpConn(ctx, t, nil, func(req *ssh.Request) {
			if !req.WantReply {
				t.Errorf("keepalive pings must have WantReply set")
			}
			requestsReceived <- req
			req.Reply(true, []byte{})
		})

		// Sending on this channel triggers a keepalive ping.
		keepaliveTicks := make(chan time.Time)
		go conn.keepalive(ctx, keepaliveTicks, nil)

		keepaliveTicks <- time.Now()

		select {
		case <-requestsReceived:
		case <-time.After(testTimeout):
			t.Errorf("didn't receive keepalive ping after trigger sent")
		}
	})

	t.Run("disconnects conn if keepalive times out", func(t *testing.T) {
		conn, _ := setUpConn(ctx, t, nil, func(req *ssh.Request) {
			req.Reply(true, []byte{})
		})

		disconnects := make(chan struct{})
		conn.RegisterDisconnectListener(disconnects)

		// Sending on this channel triggers the timeout handling mechanism for
		// the next keepalive ping.
		keepaliveTimeouts := make(chan time.Time, 1)
		keepaliveTimeouts <- time.Now()

		keepaliveTicks := make(chan time.Time)
		go conn.keepalive(ctx, keepaliveTicks, func() <-chan time.Time {
			return keepaliveTimeouts
		})

		keepaliveTicks <- time.Now()

		assertChannelClosed(t, disconnects, "keepalive failure should have disconnected the conn")
	})

	t.Run("disconnects conn if keepalive fails", func(t *testing.T) {
		conn, server := setUpConn(ctx, t, nil, func(req *ssh.Request) {
			req.Reply(true, []byte{})
		})

		disconnects := make(chan struct{})
		conn.RegisterDisconnectListener(disconnects)

		// The first keepalive request should fail immediately if the server is
		// stopped.
		server.stop()

		keepaliveTicks := make(chan time.Time)
		keepaliveComplete := make(chan struct{})
		go func() {
			conn.keepalive(ctx, keepaliveTicks, nil)
			close(keepaliveComplete)
		}()

		keepaliveTicks <- time.Now()

		assertChannelClosed(t, disconnects, "a keepalive failure didn't disconnect the conn")
		assertChannelClosed(t, keepaliveComplete, "a keepalive failure didn't terminate the keepalive goroutine")
	})

	t.Run("stops sending when conn is closed", func(t *testing.T) {
		conn, _ := setUpConn(ctx, t, nil, func(req *ssh.Request) {
			req.Reply(true, []byte{})
		})

		keepaliveComplete := make(chan struct{})
		go func() {
			conn.keepalive(ctx, nil, nil)
			close(keepaliveComplete)
		}()

		disconnects := make(chan struct{})
		conn.RegisterDisconnectListener(disconnects)

		conn.Close()

		assertChannelClosed(t, disconnects, "conn.Close() didn't disconnect the conn")
		assertChannelClosed(t, keepaliveComplete, "conn.Close() didn't terminate the keepalive goroutine")
	})
}

func TestRun(t *testing.T) {
	ctx := context.Background()

	t.Run("runs a command", func(t *testing.T) {
		// Set up a server that will respond to a command:
		//
		// "pass": with "pass stdout" as STDOUT, "pass stderr" as STDERR.
		// "fail": with "fail stdout" as STDOUT, "failstderr" as STDERR.
		client, _ := setUpClient(
			ctx,
			t,
			onNewExecChannel(func(cmd string, stdout io.Writer, stderr io.Writer) int {
				switch cmd {
				case "pass":
					stdout.Write([]byte("pass stdout"))
					stderr.Write([]byte("pass stderr"))
					return 0
				case "fail":
					stdout.Write([]byte("fail stdout"))
					stderr.Write([]byte("fail stderr"))
					return 1
				default:
					t.Errorf("unexpected command %q", cmd)
					return 255
				}
			}),
			nil,
		)

		check := func(cmd string, expectedExitStatus int, expectedStdout string, expectedStderr string) {
			var stdout bytes.Buffer
			var stderr bytes.Buffer
			err := client.Run(ctx, []string{cmd}, &stdout, &stderr)
			if expectedExitStatus == 0 {
				if err != nil {
					t.Errorf("command %q failed: %v", cmd, err)
				}
			} else if err != nil {
				if e, ok := err.(*ssh.ExitError); !ok || e.ExitStatus() != expectedExitStatus {
					t.Errorf("command %q failed: %v", cmd, err)
				}
			}
			actualStdout := stdout.String()
			actualStderr := stderr.String()

			if expectedStdout != actualStdout {
				t.Errorf("expected stdout for %q to be %q, not %q", cmd, expectedStdout, actualStdout)
			}
			if expectedStderr != actualStderr {
				t.Errorf("expected stderr for %q to be %q, not %q", cmd, expectedStderr, actualStderr)
			}
		}

		check("pass", 0, "pass stdout", "pass stderr")
		check("fail", 1, "fail stdout", "fail stderr")
	})

	t.Run("exits early if context canceled during handshake", func(t *testing.T) {
		accepted := make(chan struct{})

		done := make(chan struct{})
		defer close(done)

		// Spawn a server goroutine that will accept a connection, but never read
		// or write to the socket.
		listener, err := net.Listen("tcp", ":0")
		if err != nil {
			t.Fatalf("failed to listen on port: %v", err)
		}
		defer listener.Close()

		serverErrs := make(chan error)
		go func() {
			conn, err := listener.Accept()
			if err != nil {
				serverErrs <- err
				return
			}

			close(accepted)

			// Wait for the test to complete before closing the
			// connection. Otherwise we'll race with the OS
			// observing the closed connection with the context to
			// be canceled.
			<-done

			conn.Close()

		}()

		_, clientConfig, err := genSSHConfig()
		if err != nil {
			t.Fatalf("failed to create ssh config: %v", err)
		}

		// In order to test that we can break out of a stuck client, we
		// can use either a context.WithCancel or context.WithDeadline.
		// Since these both have similar cancelation mechanisms, we can
		// use either to verify we can interrupt the connection. Since
		// it's picky to pick the right deadline values in a test,
		// we'll use a context.WithCancel.
		connectCtx, cancel := context.WithCancel(ctx)
		defer cancel()

		// Spawn an ssh client goroutine, that will connect to the server, and err
		// out when the connection is canceled.
		connectErrs := make(chan error)

		go func() {
			client, err := connect(connectCtx, listener.Addr(), clientConfig, retry.NoRetries())
			if client != nil {
				client.Close()
			}
			connectErrs <- err
		}()

		// Wait for the connection to be accepted.
		select {
		case <-time.After(testTimeout):
			t.Fatalf("server didn't accept the connection in time")
			return
		case err := <-serverErrs:
			if err != nil {
				t.Errorf("server failed to accept connection: %v", err)
			}
		case <-accepted:
		}

		// Now that we know the connection has been accepted, we can
		// cancel the context to cause the `connect()` function to err out.
		cancel()

		// Wait for the connection to be canceled.
		select {
		case <-time.After(testTimeout):
			t.Errorf("canceling the context should cause connect() to exit")
		case err := <-connectErrs:
			if !errors.Is(err, context.Canceled) {
				t.Errorf("context was canceled but connect() returned wrong error: %v", err)
			}
		}
	})

	t.Run("exits early if context canceled while creating session", func(t *testing.T) {
		// By not passing an `onNewChannel` function we ensure that the command
		// will hang until the context is canceled.
		conn, _ := setUpConn(ctx, t, nil, nil)

		ctx, cancel := context.WithCancel(ctx)
		errs := make(chan error)
		go func() {
			errs <- conn.Run(ctx, []string{"foo"}, nil, nil)
		}()

		cancel()

		select {
		case <-time.After(testTimeout):
			t.Errorf("canceling the context should cause Run() to exit")
		case err := <-errs:
			if !errors.Is(err, context.Canceled) {
				t.Errorf("context was canceled but Run() returned wrong error: %v", err)
			}
		}
	})

	t.Run("exits early if session creation fails", func(t *testing.T) {
		conn, server := setUpConn(ctx, t, nil, nil)

		server.stop()

		errs := make(chan error)
		go func() {
			errs <- conn.Run(ctx, []string{"foo"}, nil, nil)
		}()

		select {
		case <-time.After(testTimeout):
			t.Errorf("Run() should exit if the server becomes unavailable")
		case err := <-errs:
			if err == nil {
				t.Errorf("Run() should return an error if the server is unavailable")
			}
		}
	})
}
