// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"bytes"
	"context"
	"io"
	"strconv"
	"sync/atomic"
	"testing"

	"golang.org/x/crypto/ssh"
)

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

	client, err = NewClient(ctx, server.addr, server.clientConfig)
	if err != nil {
		t.Fatalf("failed to create client: %v", err)
	}

	cleanup = func() {
		select {
		case <-client.conn.shuttingDown:
		default:
			client.Close()
		}
		server.stop()
	}
	return
}

func TestReconnect(t *testing.T) {
	ctx := context.Background()

	t.Run("can run a command before and after reconnection", func(t *testing.T) {
		var execCount int64
		client, _, cleanup := setUpClient(
			ctx,
			t,
			onNewExecChannel(func(cmd string, stdout io.Writer, stderr io.Writer) int {
				expected := strconv.Itoa(int(atomic.AddInt64(&execCount, 1)))
				if expected != cmd {
					t.Fatalf("expected exec cmd to be %q, not %q", expected, cmd)
				}
				stdout.Write([]byte(expected))
				stderr.Write([]byte(expected))
				return 0
			}),
			nil,
		)
		defer cleanup()

		// Check we can run a command before reconnecting.
		var stdout bytes.Buffer
		var stderr bytes.Buffer
		if err := client.Run(ctx, []string{"1"}, &stdout, &stderr); err != nil {
			t.Errorf("failed to run a command: %v", err)
		}
		if execCount != 1 {
			t.Errorf("expected exec count to be 1, not %d", execCount)
		}
		if stdout.String() != "1" {
			t.Errorf("expected stdout to be \"1\", not %q", stdout.String())
		}
		if stderr.String() != "1" {
			t.Errorf("expected stderr to be \"1\", not %q", stdout.String())
		}

		disconnects := make(chan struct{})
		client.RegisterDisconnectListener(disconnects)

		client.Close()

		assertChannelClosed(t, disconnects, "close should have disconnected the client")

		if err := client.Reconnect(ctx); err != nil {
			t.Errorf("failed to reconnect: %v", err)
		}

		// Check we can still run a command after reconnecting.
		stdout.Reset()
		stderr.Reset()
		if err := client.Run(ctx, []string{"2"}, &stdout, &stderr); err != nil {
			t.Errorf("failed to run a command: %v", err)
		}
		if execCount != 2 {
			t.Errorf("expected exec count to be 2, not %d", execCount)
		}
		if stdout.String() != "2" {
			t.Errorf("expected stdout to be \"2\", not %q", stdout.String())
		}
		if stderr.String() != "2" {
			t.Errorf("expected stderr to be \"2\", not %q", stdout.String())
		}
	})
}
