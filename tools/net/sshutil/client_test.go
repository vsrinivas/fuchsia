// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"errors"
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

	client, err = connect(ctx, server.addr.String(), server.clientConfig)
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
		go client.keepalive(keepaliveTicks, nil)

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

		go client.keepalive(nil, func() <-chan time.Time {
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
			client.keepalive(nil, nil)
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
			client.keepalive(nil, nil)
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

	t.Run("stops running command if context canceled", func(t *testing.T) {
		// By not passing an `onNewChannel` function we ensure that the command
		// will hang until the context is canceled.
		client, _, cleanup := setUpClient(ctx, t, nil, nil)
		defer cleanup()

		ctx, cancel := context.WithCancel(ctx)
		errs := make(chan error)
		go func() {
			errs <- client.Run(ctx, "foo", nil, nil)
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
