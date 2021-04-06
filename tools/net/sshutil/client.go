// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"io"
	"net"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

type Client struct {
	resolver Resolver
	addr     net.Addr
	config   *ssh.ClientConfig

	// The backoff that will be used when trying to establish a connection to
	// the remote.
	connectBackoff retry.Backoff

	// The following fields are protected by this mutex.
	mu        sync.Mutex
	conn      *Conn
	connected bool
}

// NewClient creates a new ssh client to the address.
func NewClient(
	ctx context.Context,
	resolver Resolver,
	config *ssh.ClientConfig,
	connectBackoff retry.Backoff,
) (*Client, error) {
	conn, err := newConn(ctx, resolver, config, connectBackoff)
	if err != nil {
		return nil, err
	}

	return &Client{
		resolver:       resolver,
		config:         config,
		connectBackoff: connectBackoff,
		conn:           conn,
		connected:      true,
	}, nil
}

// Close the ssh client connection.
func (c *Client) Close() {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.connected {
		c.conn.Close()
		c.connected = false
	}
}

// DisconnectionListener returns a channel that is closed when the client is
// disconnected.
func (c *Client) DisconnectionListener() <-chan struct{} {
	return c.conn.DisconnectionListener()
}

// Reconnect will disconnect and then reconnect the client, using the client's
// `connectBackoff` to determine the retry strategy.
func (c *Client) Reconnect(ctx context.Context) error {
	return c.ReconnectWithBackoff(ctx, c.connectBackoff)
}

// ReconnectWithBackoff will disconnect the client from the server if connected,
// then reconnect to the server, with a retry strategy based on the given
// backoff.
func (c *Client) ReconnectWithBackoff(ctx context.Context, backoff retry.Backoff) error {
	// Disconnect if we are connected.
	c.Close()

	conn, err := newConn(ctx, c.resolver, c.config, backoff)
	if err != nil {
		return err
	}

	// We don't hold the lock during the connection attempt, since it could
	// take an unbounded amount of time due to the reconnection policy.
	// However this means it's possible for a caller to call
	// ReconnectWithBackoff, and thus multiple connections. So after we
	// connect, grab the lock, and make sure we only track one connection.
	c.mu.Lock()
	if c.connected {
		c.mu.Unlock()

		conn.Close()
	} else {
		c.conn = conn
		c.connected = true

		c.mu.Unlock()
	}

	return nil
}

// Start a command on the remote device and write STDOUT and STDERR to the
// passed in io.Writers
func (c *Client) Start(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) (*Session, error) {
	c.mu.Lock()
	conn := c.conn
	c.mu.Unlock()

	return conn.Start(ctx, command, stdout, stderr)
}

// Run a command to completion on the remote device and write STDOUT and STDERR
// to the passed in io.Writers.
func (c *Client) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	c.mu.Lock()
	conn := c.conn
	c.mu.Unlock()

	return conn.Run(ctx, command, stdout, stderr)
}

// LocalAddr returns the local address being used by the underlying ssh.Client.
func (c *Client) LocalAddr() net.Addr {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.conn.mu.Lock()
	defer c.conn.mu.Unlock()
	return c.conn.mu.client.LocalAddr()
}

// NewSFTPClient returns an SFTP client that uses the currently underlying
// ssh.Client. The SFTP client will become unresponsive if the ssh connection is
// closed and/or refreshed.
func (c *Client) NewSFTPClient() (*sftp.Client, error) {
	c.mu.Lock()
	conn := c.conn
	c.mu.Unlock()
	return conn.NewSFTPClient()
}
