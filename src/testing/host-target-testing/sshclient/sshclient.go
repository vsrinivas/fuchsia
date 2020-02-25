// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshclient

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"strings"
	"sync"
	"time"

	"golang.org/x/crypto/ssh"
)

// Client is a wrapper around ssh that supports keepalive and auto-reconnection.
type Client struct {
	addr              string
	config            *ssh.ClientConfig
	shuttingDown      bool
	done              chan struct{}
	keepaliveDuration time.Duration

	// This mutex protects the following fields
	mu                     sync.Mutex
	client                 *ssh.Client
	conn                   net.Conn
	connectionListeners    []*sync.WaitGroup
	disconnectionListeners []*sync.WaitGroup
}

// NewClient creates a new ssh client to the address.
func NewClient(ctx context.Context, addr string, config *ssh.ClientConfig) (*Client, error) {
	c := &Client{
		addr:              addr,
		config:            config,
		client:            nil,
		conn:              nil,
		shuttingDown:      false,
		done:              make(chan struct{}),
		keepaliveDuration: 10 * time.Second,
	}
	go c.keepalive()

	// Wait for the initial connection, so we don't return a client that's
	// in a "just created but not connected" state.
	if err := c.WaitToBeConnected(ctx); err != nil {
		return nil, err
	}

	return c, nil
}

// Helper to create an ssh client and connection to the address.
func connect(ctx context.Context, addr string, config *ssh.ClientConfig) (*ssh.Client, net.Conn, error) {
	d := net.Dialer{Timeout: config.Timeout}

	conn, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, nil, err
	}
	clientConn, chans, reqs, err := ssh.NewClientConn(conn, addr, config)
	if err != nil {
		conn.Close()
		return nil, nil, err
	}
	client := ssh.NewClient(clientConn, chans, reqs)

	return client, conn, nil
}

func (c *Client) makeSession(ctx context.Context, stdout io.Writer, stderr io.Writer) (*Session, error) {
	// Temporarily grab the lock and make a copy of the client. This
	// prevents a long running `Run` command from blocking the keep-alive
	// goroutine.
	c.mu.Lock()
	client := c.client
	c.mu.Unlock()

	if client == nil {
		return nil, fmt.Errorf("ssh is disconnected")
	}

	type result struct {
		session *Session
		err     error
	}

	ch := make(chan result)
	go func() {
		session, err := client.NewSession()
		if err != nil {
			ch <- result{session: nil, err: err}
		}

		session.Stdout = stdout
		session.Stderr = stderr

		s := Session{session: session}

		ch <- result{session: &s, err: nil}
	}()

	select {
	case r := <-ch:
		return r.session, r.err
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// Start a command on the remote device and write STDOUT and STDERR to the
// passed in io.Writers.
func (c *Client) Start(ctx context.Context, command string, stdout io.Writer, stderr io.Writer) (*Session, error) {
	session, err := c.makeSession(ctx, stdout, stderr)
	if err != nil {
		return nil, err
	}

	log.Printf("spawning: %s", command)

	if err := session.Start(ctx, command); err != nil {
		session.Close()
		return nil, err
	}
	return session, nil
}

// Run a command to completion on the remote device and write STDOUT and STDERR
// to the passed in io.Writers.
func (c *Client) Run(ctx context.Context, command string, stdout io.Writer, stderr io.Writer) error {
	session, err := c.makeSession(ctx, stdout, stderr)
	if err != nil {
		return err
	}
	defer session.Close()

	log.Printf("running: %s", command)

	return session.Run(ctx, command)
}

// Close the ssh client connections.
func (c *Client) Close() {
	c.shuttingDown = true
	close(c.done)
	c.disconnect()
}

// GetSshConnection returns the first field in the remote SSH_CONNECTION
// environment variable.
func (c *Client) GetSshConnection(ctx context.Context) (string, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := c.Run(ctx, "PATH= echo $SSH_CONNECTION", &stdout, &stderr)
	if err != nil {
		return "", fmt.Errorf("failed to read SSH_CONNECTION: %s: %s", err, string(stderr.Bytes()))
	}
	return strings.Split(string(stdout.Bytes()), " ")[0], nil
}

// WaitToBeConnected blocks until the ssh connection is available for use.
func (c *Client) WaitToBeConnected(ctx context.Context) error {
	log.Printf("waiting for %s to to connect", c.addr)

	var wg sync.WaitGroup
	c.RegisterConnectListener(&wg)

	ch := make(chan struct{})
	go func() {
		defer close(ch)
		wg.Wait()
	}()

	select {
	case <-ch:
		log.Printf("connected to %s", c.addr)
		return nil
	case <-ctx.Done():
		return ctx.Err()
	}
}

// RegisterConnectListener adds a waiter that gets notified when the ssh
// client is connected.
func (c *Client) RegisterConnectListener(wg *sync.WaitGroup) {
	wg.Add(1)

	c.mu.Lock()
	if c.client == nil {
		c.connectionListeners = append(c.connectionListeners, wg)
	} else {
		wg.Done()
	}
	c.mu.Unlock()
}

// RegisterDisconnectListener adds a waiter that gets notified when the ssh
// client is disconnected.
func (c *Client) RegisterDisconnectListener(wg *sync.WaitGroup) {
	wg.Add(1)

	c.mu.Lock()
	if c.client == nil {
		wg.Done()
	} else {
		c.disconnectionListeners = append(c.disconnectionListeners, wg)
	}
	c.mu.Unlock()
}

// IsConnected checks if we are currently connected to the server.
func (c *Client) IsConnected() bool {
	c.mu.Lock()
	defer c.mu.Unlock()

	return c.client != nil
}

// disconnect from ssh, and notify anyone waiting for disconnection.
func (c *Client) disconnect() {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.disconnectLocked()
}

func (c *Client) disconnectLocked() {
	log.Printf("disconnected from %s", c.addr)

	if c.client != nil {
		c.client.Close()
		c.client = nil
	}

	for _, listener := range c.disconnectionListeners {
		listener.Done()
	}
	c.disconnectionListeners = []*sync.WaitGroup{}
}

// Make a single attempt to reconnect to the ssh server.
func (c *Client) reconnectLocked(ctx context.Context) {
	// We can exit early if we are shutting down, or we already have a client.
	if c.shuttingDown || c.client != nil {
		return
	}

	log.Printf("attempting to reconnect to %s...", c.addr)

	client, conn, err := connect(ctx, c.addr, c.config)
	if err == nil {
		c.client = client
		c.conn = conn

		for _, listener := range c.connectionListeners {
			listener.Done()
		}
		c.connectionListeners = []*sync.WaitGroup{}

		log.Printf("reconnected to %s", c.addr)
	} else {
		log.Printf("reconnection failed: %s", err)
	}
}

// Send periodic keep-alives. If we don't do this, then we might not observe
// the server side disconnecting from us.
func (c *Client) keepalive() {
	ctx := context.Background()

	c.emitKeepalive(ctx)

	for {
		select {
		case <-time.After(c.keepaliveDuration):
			c.emitKeepalive(ctx)
		case <-c.done:
			return
		}
	}
}

// Emit a heartbeat to the ssh server.
func (c *Client) emitKeepalive(ctx context.Context) {
	// If the client is disconnected from the server, attempt to reconnect.
	// Otherwise, emit a heartbeat.
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.client == nil {
		c.reconnectLocked(ctx)
		return
	}

	// ssh keepalive is not completely reliable. So in addition to emitting
	// it, we'll also set a tcp deadline to timeout if we don't get a
	// keepalive response within some period of time.
	c.conn.SetDeadline(time.Now().Add(c.keepaliveDuration * 2))

	_, _, err := c.client.SendRequest("keepalive@openssh.com", true, nil)
	if err != nil {
		if !c.shuttingDown {
			log.Printf("disconnected from %s: %s", c.addr, err)
		}
		c.disconnectLocked()
	}
}

// Session is a wrapper around ssh.Session that allows operations to be canceled.
type Session struct {
	session *ssh.Session
}

func (s *Session) Close() {
	s.session.Close()
}

func (s *Session) Start(ctx context.Context, command string) error {
	ch := make(chan error)
	go func() {
		ch <- s.session.Start(command)
	}()

	select {
	case err := <-ch:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (s *Session) Wait(ctx context.Context) error {
	ch := make(chan error)
	go func() {
		ch <- s.session.Wait()
	}()

	select {
	case err := <-ch:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}

}

func (s *Session) Run(ctx context.Context, command string) error {
	ch := make(chan error)
	go func() {
		ch <- s.session.Run(command)
	}()

	select {
	case err := <-ch:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}
