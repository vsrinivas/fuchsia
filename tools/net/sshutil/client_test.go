// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"bytes"
	"context"
	"errors"
	"io"
	"testing"
	"time"

	"golang.org/x/crypto/ssh"
)

const testTimeout = 5 * time.Second

func setUpClient(
	ctx context.Context,
	t *testing.T,
	onNewChannel func(ssh.NewChannel),
	onRequest func(*ssh.Request),
) (client *Client, server *sshServer, cleanup func()) {
	server, err := startSSHServer(onNewChannel, onRequest)
	if err != nil {
		t.Fatalf("failed to start ssh server: %v", err)
	}
	defer func() {
		if client == nil {
			server.stop()
		}
	}()

	client, err = connect(ctx, server.addr, server.clientConfig)
	if err != nil {
		t.Fatalf("failed to create client: %v", err)
	}

	cleanup = func() {
		select {
		// Only close the client if it hasn't already been closed.
		case <-client.shuttingDown:
		default:
			client.Close()
		}
		server.stop()
	}
	return
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

		client, _, cleanup := setUpClient(ctx, t, nil, func(req *ssh.Request) {
			if !req.WantReply {
				t.Errorf("keepalive pings must have WantReply set")
			}
			requestsReceived <- req
			req.Reply(true, []byte{})
		})
		defer cleanup()

		// Sending on this channel triggers a keepalive ping. keepalive() also
		// sends an initial ping immediately when it's called.
		keepaliveTicks := make(chan time.Time)
		go client.keepalive(ctx, keepaliveTicks, nil)

		select {
		case <-requestsReceived:
		case <-time.After(testTimeout):
			t.Errorf("didn't receive keepalive ping on startup")
		}

		keepaliveTicks <- time.Now()

		select {
		case <-requestsReceived:
		case <-time.After(testTimeout):
			t.Errorf("didn't receive keepalive ping after trigger sent")
		}
	})

	t.Run("disconnects client if keepalive times out", func(t *testing.T) {
		client, _, cleanup := setUpClient(ctx, t, nil, func(req *ssh.Request) {
			req.Reply(true, []byte{})
		})
		defer cleanup()

		disconnects := make(chan struct{})
		client.RegisterDisconnectListener(disconnects)

		// Sending on this channel triggers the timeout handling mechanism for
		// the next keepalive ping.
		keepaliveTimeouts := make(chan time.Time, 1)
		keepaliveTimeouts <- time.Now()

		go client.keepalive(ctx, nil, func() <-chan time.Time {
			return keepaliveTimeouts
		})

		assertChannelClosed(t, disconnects, "keepalive failure should have disconnected the client")
	})

	t.Run("disconnects client if keepalive fails", func(t *testing.T) {
		client, server, cleanup := setUpClient(ctx, t, nil, func(req *ssh.Request) {
			req.Reply(true, []byte{})
		})
		defer cleanup()

		disconnects := make(chan struct{})
		client.RegisterDisconnectListener(disconnects)

		// The first keepalive request should fail immediately if the server is
		// stopped.
		server.stop()

		keepaliveComplete := make(chan struct{})
		go func() {
			client.keepalive(ctx, nil, nil)
			close(keepaliveComplete)
		}()

		assertChannelClosed(t, disconnects, "a keepalive failure didn't disconnect the client")
		assertChannelClosed(t, keepaliveComplete, "a keepalive failure didn't terminate the keepalive goroutine")
	})

	t.Run("stops sending when client is closed", func(t *testing.T) {
		client, _, cleanup := setUpClient(ctx, t, nil, func(req *ssh.Request) {
			req.Reply(true, []byte{})
		})
		defer cleanup()

		keepaliveComplete := make(chan struct{})
		go func() {
			client.keepalive(ctx, nil, nil)
			close(keepaliveComplete)
		}()

		disconnects := make(chan struct{})
		client.RegisterDisconnectListener(disconnects)

		client.Close()

		assertChannelClosed(t, disconnects, "client.Close() didn't disconnect the client")
		assertChannelClosed(t, keepaliveComplete, "client.Close() didn't terminate the keepalive goroutine")
	})
}

func TestRun(t *testing.T) {
	ctx := context.Background()

	t.Run("runs a command", func(t *testing.T) {
		// Set up a server that will respond to a command:
		//
		// "pass": with "pass stdout" as STDOUT, "pass stderr" as STDERR.
		// "fail": with "fail stdout" as STDOUT, "failstderr" as STDERR.
		client, _, cleanup := setUpClient(
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
		defer cleanup()

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

	t.Run("stops running command if context canceled", func(t *testing.T) {
		// By not passing an `onNewChannel` function we ensure that the command
		// will hang until the context is canceled.
		client, _, cleanup := setUpClient(ctx, t, nil, nil)
		defer cleanup()

		ctx, cancel := context.WithCancel(ctx)
		errs := make(chan error)
		go func() {
			errs <- client.Run(ctx, []string{"foo"}, nil, nil)
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
}
