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

func setUpConn(
	ctx context.Context,
	t *testing.T,
	onNewChannel func(ssh.NewChannel),
	onRequest func(ssh.Channel, *ssh.Request),
) (*Conn, *sshServer) {
	server, err := startSSHServer(ctx, onNewChannel, onRequest)
	if err != nil {
		t.Fatalf("failed to start ssh server: %s", err)
	}
	t.Cleanup(func() {
		if err := server.stop(); err != nil && !errors.Is(err, io.EOF) {
			t.Error(err)
		}
	})

	conn, err := connect(
		ctx,
		ConstantAddrResolver{
			Addr: server.listener.Addr(),
		},
		server.clientConfig,
		retry.NoRetries(),
	)
	if err != nil {
		t.Fatalf("failed to create conn: %s", err)
	}
	t.Cleanup(func() {
		_ = conn.Close()
	})

	return conn, server
}

func TestKeepalive(t *testing.T) {
	ctx := context.Background()

	t.Run("sends pings when timer fires", func(t *testing.T) {
		requestsReceived := make(chan *ssh.Request, 1)

		conn, _ := setUpConn(ctx, t, nil, func(_ ssh.Channel, req *ssh.Request) {
			if !req.WantReply {
				t.Errorf("keepalive pings must have WantReply set")
			}
			requestsReceived <- req
			if err := req.Reply(true, nil); err != nil && !errors.Is(err, io.EOF) {
				t.Error(err)
			}
		})

		// Sending on this channel triggers a keepalive ping.
		keepaliveTicks := make(chan time.Time)
		session, err := conn.mu.client.NewSession()
		if err != nil {
			t.Fatal(err)
		}
		go conn.keepalive(ctx, session, keepaliveTicks, nil)

		keepaliveTicks <- time.Now()

		<-requestsReceived
	})

	t.Run("disconnects conn if keepalive times out", func(t *testing.T) {
		conn, _ := setUpConn(ctx, t, nil, func(_ ssh.Channel, req *ssh.Request) {
			if err := req.Reply(true, nil); err != nil {
				t.Error(err)
			}
		})

		// Sending on this channel triggers the timeout handling mechanism for
		// the next keepalive ping.
		keepaliveTimeouts := make(chan time.Time, 1)
		keepaliveTimeouts <- time.Now()

		keepaliveTicks := make(chan time.Time)
		session, err := conn.mu.client.NewSession()
		if err != nil {
			t.Fatal(err)
		}
		go conn.keepalive(ctx, session, keepaliveTicks, func() <-chan time.Time {
			return keepaliveTimeouts
		})

		keepaliveTicks <- time.Now()

		<-conn.DisconnectionListener()
	})

	t.Run("disconnects conn if keepalive fails", func(t *testing.T) {
		conn, server := setUpConn(ctx, t, nil, func(_ ssh.Channel, req *ssh.Request) {
			if err := req.Reply(true, nil); err != nil {
				t.Error(err)
			}
		})

		session, err := conn.mu.client.NewSession()
		if err != nil {
			t.Fatal(err)
		}

		// The first keepalive request should fail immediately if the server is
		// stopped.
		if err := server.stop(); err != nil {
			t.Error(err)
		}

		keepaliveTicks := make(chan time.Time)
		keepaliveComplete := make(chan struct{})
		go func() {
			conn.keepalive(ctx, session, keepaliveTicks, nil)
			close(keepaliveComplete)
		}()

		keepaliveTicks <- time.Now()

		<-conn.DisconnectionListener()
		<-keepaliveComplete
	})

	t.Run("stops sending when conn is closed", func(t *testing.T) {
		conn, _ := setUpConn(ctx, t, nil, func(_ ssh.Channel, req *ssh.Request) {
			if err := req.Reply(true, nil); err != nil {
				t.Error(err)
			}
		})

		keepaliveComplete := make(chan struct{})
		session, err := conn.mu.client.NewSession()
		if err != nil {
			t.Fatal(err)
		}
		go func() {
			conn.keepalive(ctx, session, nil, nil)
			close(keepaliveComplete)
		}()

		if err := conn.Close(); err != nil {
			t.Error(err)
		}

		<-conn.DisconnectionListener()
		<-keepaliveComplete
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
			nil,
			onExecRequest(func(cmd string, stdout io.Writer, stderr io.Writer) int {
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
		)

		check := func(cmd string, expectedExitStatus int, expectedStdout string, expectedStderr string) {
			var stdout bytes.Buffer
			var stderr bytes.Buffer
			err := client.Run(ctx, []string{cmd}, &stdout, &stderr)
			if expectedExitStatus == 0 {
				if err != nil {
					t.Errorf("command %q failed: %s", cmd, err)
				}
			} else if err != nil {
				if e, ok := err.(*ssh.ExitError); !ok || e.ExitStatus() != expectedExitStatus {
					t.Errorf("command %q failed: %s", cmd, err)
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
			t.Fatalf("failed to listen on port: %s", err)
		}
		defer func() {
			if err := listener.Close(); err != nil {
				t.Log(err)
			}
		}()

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

			if err := conn.Close(); err != nil {
				t.Error(err)
			}
		}()

		_, clientConfig, err := genSSHConfig()
		if err != nil {
			t.Fatalf("failed to create ssh config: %s", err)
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
			client, err := connect(
				connectCtx,
				ConstantAddrResolver{
					Addr: listener.Addr(),
				},
				clientConfig,
				retry.NoRetries(),
			)
			if client != nil {
				if err := client.Close(); err != nil {
					t.Error(err)
				}
			}
			connectErrs <- err
		}()

		// Wait for the connection to be accepted.
		select {
		case err := <-serverErrs:
			if err != nil {
				t.Errorf("server failed to accept connection: %s", err)
			}
		case <-accepted:
		}

		// Now that we know the connection has been accepted, we can
		// cancel the context to cause the `connect()` function to err out.
		cancel()

		// Wait for the connection to be canceled.
		select {
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
		case err := <-errs:
			if !errors.Is(err, context.Canceled) {
				t.Errorf("context was canceled but Run() returned wrong error: %v", err)
			}
		}
	})

	t.Run("exits early if session creation fails", func(t *testing.T) {
		conn, server := setUpConn(ctx, t, nil, nil)

		if err := server.stop(); err != nil {
			t.Error(err)
		}

		errs := make(chan error)
		go func() {
			errs <- conn.Run(ctx, []string{"foo"}, nil, nil)
		}()

		select {
		case err := <-errs:
			if err == nil {
				t.Errorf("Run() should return an error if the server is unavailable")
			}
		}
	})
}
