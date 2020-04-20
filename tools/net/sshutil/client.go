// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"strings"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"

	"golang.org/x/crypto/ssh"
)

const (
	connectInterval = 5 * time.Second

	// Interval between keep-alive pings.
	defaultKeepaliveInterval = 1 * time.Second

	// Cancel the connection if a we don't receive a response to a keep-alive
	// ping within this amount of time.
	defaultKeepaliveTimeout = defaultKeepaliveInterval + 15*time.Second
)

// Client is a wrapper around ssh that supports keepalive and auto-reconnection.
// TODO(fxb/48042): change all usage of sshutil to use this Client type instead
// of ssh.Client.
type Client struct {
	*ssh.Client

	addr         net.Addr
	config       *ssh.ClientConfig
	shuttingDown chan struct{}

	// This mutex protects the following fields
	mu                     sync.Mutex
	disconnectionListeners []chan struct{}
}

// NewClient creates a new ssh client to the address and launches a goroutine to
// send keep-alive pings as long as the client is connected.
func NewClient(ctx context.Context, addr net.Addr, config *ssh.ClientConfig) (*Client, error) {
	client, err := connect(ctx, addr, config)
	if err != nil {
		return nil, err
	}
	go func() {
		t := time.NewTicker(defaultKeepaliveInterval)
		defer t.Stop()
		timeout := func() <-chan time.Time {
			return time.After(defaultKeepaliveTimeout)
		}
		client.keepalive(t.C, timeout)
	}()
	return client, nil
}

// connect continously attempts to connect to a remote server, and returns an
// ssh client if successful, or errs out if the context is canceled.
func connect(ctx context.Context, addr net.Addr, config *ssh.ClientConfig) (*Client, error) {
	var client *ssh.Client
	err := retry.Retry(ctx, retry.NewConstantBackoff(connectInterval), func() error {
		log.Printf("trying to connect to %s...", addr)
		var err error
		client, err = connectToSSH(ctx, addr, config)
		if err != nil {
			log.Printf("failed to connect, will try again in %s: %s", connectInterval, err)
			return err
		}
		log.Printf("connected to %s", addr)
		return nil
	}, nil)
	if err != nil {
		return nil, err
	}
	return &Client{
		Client:       client,
		addr:         addr,
		config:       config,
		shuttingDown: make(chan struct{}),
	}, nil
}

func connectToSSH(ctx context.Context, addr net.Addr, config *ssh.ClientConfig) (*ssh.Client, error) {
	d := net.Dialer{Timeout: config.Timeout}
	conn, err := d.DialContext(ctx, "tcp", addr.String())
	if err != nil {
		return nil, err
	}

	// We made a TCP connection, now establish an SSH connection over it.
	clientConn, chans, reqs, err := ssh.NewClientConn(conn, addr.String(), config)
	if err != nil {
		if closeErr := conn.Close(); closeErr != nil {
			return nil, fmt.Errorf(
				"error closing connection: %v; original error: %w", closeErr, err)
		}
		return nil, err
	}
	return ssh.NewClient(clientConn, chans, reqs), nil
}

func (c *Client) makeSession(ctx context.Context, stdout io.Writer, stderr io.Writer) (*Session, error) {
	// Temporarily grab the lock and make a copy of the client. This
	// prevents a long running `Run` command from blocking the keep-alive
	// goroutine.
	c.mu.Lock()
	client := c.Client
	c.mu.Unlock()

	if client == nil {
		return nil, fmt.Errorf("ssh is disconnected")
	}

	type result struct {
		session *Session
		err     error
	}

	// Use a buffered channel to ensure that sending the first element doesn't
	// block and cause the goroutine to leak in the case where the context gets
	// cancelled before we receive on the channel.
	ch := make(chan result, 1)
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
func (c *Client) Start(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) (*Session, error) {
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
func (c *Client) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
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

// RegisterDisconnectListener adds a waiter that gets notified when the ssh
// client is disconnected.
func (c *Client) RegisterDisconnectListener(ch chan struct{}) {
	c.mu.Lock()
	if c.Client == nil {
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

	if c.Client != nil {
		c.Client.Close()
		c.Client = nil
	}

	for _, listener := range c.disconnectionListeners {
		close(listener)
	}
	c.disconnectionListeners = []chan struct{}{}
}

// Send periodic keep-alives. If we don't do this, then we might not observe
// the server side disconnecting from us.
// A keep-alive ping is sent whenever we receive something on the `ticks`
// channel.
// After sending a ping, we call the `timeout` function and wait until either we
// recieve a response or we receive something on the channel returned by
// `timeout`.
func (c *Client) keepalive(ticks <-chan time.Time, timeout func() <-chan time.Time) {
	if timeout == nil {
		timeout = func() <-chan time.Time {
			return nil
		}
	}
	for {
		c.mu.Lock()
		client := c.Client
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
		ch := make(chan error, 1)
		go func() {
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

		case <-timeout():
			log.Printf("timed out sending keepalive, disconnecting")
			c.disconnect()
			return
		}

		// Otherwise, sleep until the next poll cycle.
		select {
		case <-ticks:
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

func (s *Session) Start(ctx context.Context, command []string) error {
	ch := make(chan error, 1)
	go func() {
		ch <- s.session.Start(strings.Join(command, " "))
	}()

	select {
	case err := <-ch:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (s *Session) Wait(ctx context.Context) error {
	ch := make(chan error, 1)
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

func (s *Session) Run(ctx context.Context, command []string) error {
	ch := make(chan error, 1)
	go func() {
		ch <- s.session.Run(strings.Join(command, " "))
	}()

	select {
	case err := <-ch:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}
