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

const (
	reconnectInterval = 5 * time.Second
	keepaliveInterval = 1 * time.Second
	keepaliveDeadline = keepaliveInterval + 15*time.Second
)

// Client is a wrapper around ssh that supports keepalive and auto-reconnection.
type Client struct {
	addr         string
	config       *ssh.ClientConfig
	shuttingDown chan struct{}

	// This mutex protects the following fields
	mu                     sync.Mutex
	client                 *ssh.Client
	conn                   net.Conn
	disconnectionListeners []chan struct{}
}

// NewClient creates a new ssh client to the address.
func NewClient(ctx context.Context, addr string, config *ssh.ClientConfig) (*Client, error) {
	client, conn, err := connect(ctx, addr, config)
	if err != nil {
		return nil, err
	}

	c := &Client{
		addr:         addr,
		config:       config,
		client:       client,
		conn:         conn,
		shuttingDown: make(chan struct{}),
	}

	go c.keepalive()

	return c, nil
}

// connect continously attempts to connect to a remote server, and returns an
// ssh client and connection if successful, or errs out if the context is
// canceled.
func connect(ctx context.Context, addr string, config *ssh.ClientConfig) (*ssh.Client, net.Conn, error) {
	t := time.NewTicker(reconnectInterval)

	for {
		log.Printf("trying to connect to %s...", addr)

		client, conn, err := connectToSSH(ctx, addr, config)
		if err == nil {
			log.Printf("connected to %s", addr)

			return client, conn, nil
		}

		log.Printf("failed to connect, will try again in %s: %s", reconnectInterval, err)

		select {
		case <-t.C:
			continue
		case <-ctx.Done():
			return nil, nil, ctx.Err()
		}
	}
}

func connectToSSH(ctx context.Context, addr string, config *ssh.ClientConfig) (*ssh.Client, net.Conn, error) {
	d := net.Dialer{Timeout: config.Timeout}
	conn, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, nil, err
	}

	// We made a TCP connection, now establish an SSH connection over it.
	clientConn, chans, reqs, err := ssh.NewClientConn(conn, addr, config)
	if err == nil {
		return ssh.NewClient(clientConn, chans, reqs), conn, nil
	}

	conn.Close()

	return nil, nil, err
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
			return
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
	// Notify the keepalive goroutine we are shutting down.
	close(c.shuttingDown)
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

// RegisterDisconnectListener adds a waiter that gets notified when the ssh
// client is disconnected.
func (c *Client) RegisterDisconnectListener(ch chan struct{}) {
	c.mu.Lock()
	if c.client == nil {
		close(ch)
	} else {
		c.disconnectionListeners = append(c.disconnectionListeners, ch)
	}
	c.mu.Unlock()
}

// disconnect from ssh, and notify anyone waiting for disconnection.
func (c *Client) disconnect() {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.client != nil {
		c.client.Close()
		c.client = nil
	}

	for _, listener := range c.disconnectionListeners {
		close(listener)
	}
	c.disconnectionListeners = []chan struct{}{}
}

// Send periodic keep-alives. If we don't do this, then we might not observe
// the server side disconnecting from us.
func (c *Client) keepalive() {
	t := time.NewTicker(keepaliveInterval)
	defer t.Stop()

	for {
		c.mu.Lock()
		client := c.client
		conn := c.conn
		c.mu.Unlock()

		// Exit early if the client's already been shut down.
		if client == nil {
			return
		}

		// SendRequest can actually hang if the server stops responding
		// in between receving a keepalive and sending a response (see
		// fxb/47698). To protect against this, we'll emit events in a
		// separate goroutine so if we don't get one in the expected
		// time we'll disconnect.
		ch := make(chan error)
		go func() {
			// ssh keepalive is not completely reliable. So in
			// addition to emitting it, we'll also set a tcp
			// deadline to timeout if we don't get a keepalive
			// response within some period of time.
			conn.SetDeadline(time.Now().Add(keepaliveDeadline))

			// Try to emit a keepalive message. We use a unique
			// name to distinguish ourselves from the server-side
			// keepalive name to ease debugging. If we get any
			// error, reconnect to the server.
			_, _, err := client.SendRequest("keepalive@fuchsia.com", true, nil)
			ch <- err
		}()

		select {
		case <-c.shuttingDown:
			// Ignore the keepalive result if we are shutting down.
			c.disconnect()

		case err := <-ch:
			// disconnect if we hit an error sending a keepalive.
			if err != nil {
				log.Printf("error sending keepalive to %s, disconnecting: %s", c.addr, err)
				c.disconnect()
				return
			}

		case <-time.After(keepaliveDeadline):
			log.Printf("timed out sending keepalive, disconnecting")
			c.disconnect()
			return
		}

		// Otherwise, sleep until the next poll cycle.
		select {
		case <-t.C:
		case <-c.shuttingDown:
			return
		}
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
