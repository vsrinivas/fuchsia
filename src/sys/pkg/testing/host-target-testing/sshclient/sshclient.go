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
	addr             string
	config           *ssh.ClientConfig
	shuttingDown     chan struct{}
	shutdownComplete chan struct{}

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
		addr:             addr,
		config:           config,
		client:           nil,
		conn:             nil,
		shuttingDown:     make(chan struct{}),
		shutdownComplete: make(chan struct{}),
	}
	go c.keepalive()

	// Wait for the initial connection, so we don't return a client that's
	// in a "just created but not connected" state.
	if err := c.WaitToBeConnected(ctx); err != nil {
		return nil, err
	}

	return c, nil
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
	// Notify the keepalive thread we are shutting down, and wait for it to
	// exit.
	close(c.shuttingDown)
	<-c.shutdownComplete
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
		wg.Wait()
		close(ch)
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

// disconnect from ssh, and notify anyone waiting for disconnection.
func (c *Client) disconnect() {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.disconnectLocked()
}

func (c *Client) disconnectLocked() {
	if c.client != nil {
		c.client.Close()
		c.client = nil
	}

	for _, listener := range c.disconnectionListeners {
		listener.Done()
	}
	c.disconnectionListeners = []*sync.WaitGroup{}
}

// A simple state type to track the connection/keepalive state machine.
type connectionState int

const (
	unconnectedState connectionState = iota
	connectedState
	shutdownState
)

// Send periodic keep-alives. If we don't do this, then we might not observe
// the server side disconnecting from us.
func (c *Client) keepalive() {
	reconnectTicker := time.NewTicker(reconnectInterval)
	defer reconnectTicker.Stop()

	keepaliveTicker := time.NewTicker(keepaliveInterval)
	defer keepaliveTicker.Stop()

	// We initially are not connected to the server.
	state := unconnectedState

	for {
		// Exit early if we are shutting down.
		select {
		case <-c.shuttingDown:
			break
		default:
		}

		// Otherwise run one step of our state machine.
		switch state {
		case unconnectedState:
			// Try to connect to the server. If we stated unconnected, sleep and retry.
			if state = c.connect(); state == unconnectedState {
				select {
				case <-reconnectTicker.C:
					continue
				case <-c.shuttingDown:
					break
				}
			}

		case connectedState:
			// We are still connected to the server, so send a keepalive, and retry.
			if state = c.emitKeepalive(); state == connectedState {
				select {
				case <-keepaliveTicker.C:
					continue
				case <-c.shuttingDown:
					break
				}
			}

		case shutdownState:
			break
		}
	}

	// We've shut down, so make sure we actually disconnect from the server.
	c.disconnect()
	close(c.shutdownComplete)
}

// Make a single attempt to connect to the ssh server.
func (c *Client) connect() connectionState {
	log.Printf("attempting to connect to %s...", c.addr)

	// Connect to the remote server. In order to interrupt this call if the
	// `Client` is closed during connection, we'll spawn a goroutine to do
	// the actual connection, and give it a context that we'll cancel if
	// we're shut down during connection.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	type result struct {
		client *ssh.Client
		conn   net.Conn
		err    error
	}
	ch := make(chan result)
	go func() {
		client, conn, err := connectToSSH(ctx, c.addr, c.config)
		ch <- result{client, conn, err}
	}()

	var client *ssh.Client
	var conn net.Conn
	select {
	case r := <-ch:
		if r.err != nil {
			// If the context was canceled, then shutdown the keepalive goroutine.
			if r.err == context.Canceled {
				return shutdownState
			}

			// Otherwise, try to reconnect.
			log.Printf("failed to connect, will try again in %s: %s", reconnectInterval, r.err)
			return unconnectedState
		}

		client = r.client
		conn = r.conn
	case <-c.shuttingDown:
		// The client shut down, so exit.
		return shutdownState
	}

	// We've successfully connected to the server.
	c.mu.Lock()
	defer c.mu.Unlock()

	// Since we didn't hold open the lock during the connection process,
	// it's possible the client could have been closed. Now that we have
	// the lock, check again if we've shutdown.
	select {
	case <-c.shuttingDown:
		// We shut down, so clean up the connection we just made and exit.
		client.Close()
		return shutdownState
	default:
	}

	// Otherwise, update the client with the new connection.
	c.client = client
	c.conn = conn

	// Success! Notify our connection listeners we connected.
	for _, listener := range c.connectionListeners {
		listener.Done()
	}
	c.connectionListeners = []*sync.WaitGroup{}

	// We connected, so transition to the connected state.
	return connectedState
}

// connectToSSH connects to a remote server, and returns an ssh client and
// connection if successful.
func connectToSSH(ctx context.Context, addr string, config *ssh.ClientConfig) (*ssh.Client, net.Conn, error) {
	d := net.Dialer{Timeout: config.Timeout}
	conn, err := d.DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, nil, err
	}

	// We made a TCP connection, now establish an SSH connection over it.
	clientConn, chans, reqs, err := ssh.NewClientConn(conn, addr, config)
	if err != nil {
		conn.Close()
		return nil, nil, err
	}

	return ssh.NewClient(clientConn, chans, reqs), conn, nil
}

func (c *Client) emitKeepalive() connectionState {
	c.mu.Lock()
	client := c.client
	conn := c.conn
	c.mu.Unlock()

	// ssh keepalive is not completely reliable. So in addition to emitting
	// it, we'll also set a tcp deadline to timeout if we don't get a
	// keepalive response within some period of time.
	conn.SetDeadline(time.Now().Add(keepaliveDeadline))

	// Try to emit a keepalive message. We use a unique name to distinguish
	// ourselves from the server-side keepalive name to ease debugging. If
	// we get any error, reconnect to the server.
	if _, _, err := client.SendRequest("keepalive@fuchsia.com", true, nil); err != nil {
		log.Printf("error sending keepalive to %s, reconnecting: %s", c.addr, err)
		c.disconnect()

		return unconnectedState
	}

	// We successfully sent a keepalive, so stay in the connected state.
	return connectedState
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
