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
	addr   net.Addr
	config *ssh.ClientConfig

	// The backoff that will be used when trying to establish a connection to
	// the remote.
	connectBackoff retry.Backoff

	// The following fields are protected by this mutex.
	mu        sync.Mutex
	conn      *Conn
	connected bool
}

// NewClient creates a new ssh client to the address.
func NewClient(ctx context.Context, addr net.Addr, config *ssh.ClientConfig, connectBackoff retry.Backoff) (*Client, error) {
	conn, err := newConn(ctx, addr, config, connectBackoff)
	if err != nil {
		return nil, err
	}

	return &Client{
		addr:           addr,
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

	c.conn.Close()
	c.connected = false
}

// RegisterDisconnectListener adds a waiter that gets notified when the ssh
// client is disconnected.
func (c *Client) RegisterDisconnectListener(ch chan struct{}) {
	c.mu.Lock()
	conn := c.conn
	c.mu.Unlock()

	conn.RegisterDisconnectListener(ch)
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
	c.mu.Lock()
	defer c.mu.Unlock()

	// Only disconnect if we are connected.
	if c.connected {
		c.conn.Close()
		c.connected = false
	}

	conn, err := newConn(ctx, c.addr, c.config, backoff)
	if err != nil {
		return err
	}
	c.conn = conn
	c.connected = true

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
	conn := c.conn
	c.mu.Unlock()
	return conn.LocalAddr()
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
