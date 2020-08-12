// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"strings"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

const (
	// Interval between keepalive pings.
	defaultKeepaliveInterval = 1 * time.Second

	// Cancel the connection if a we don't receive a response to a keepalive
	// ping within this amount of time.
	defaultKeepaliveTimeout = defaultKeepaliveInterval + 5*time.Second

	// A conventionally used global request name for checking the status of a client
	// connection to an OpenSSH server.
	keepaliveOpenSSH = "keepalive@openssh.com"
)

// Conn is a wrapper around ssh that supports keepalive and auto-reconnection.
type Conn struct {
	*ssh.Client

	addr         net.Addr
	config       *ssh.ClientConfig
	shuttingDown chan struct{}

	// This mutex protects the following fields
	mu                     sync.Mutex
	disconnectionListeners []chan struct{}
}

// newConn creates a new ssh client to the address and launches a goroutine to
// send keepalive pings as long as the client is connected.
func newConn(ctx context.Context, addr net.Addr, config *ssh.ClientConfig, backoff retry.Backoff) (*Conn, error) {
	conn, err := connect(ctx, addr, config, backoff)
	if err != nil {
		return nil, err
	}

	// We want to log from the keepalive thread, but we don't want to inherit
	// any of `ctx`'s cancellations. So we will create a new context and
	// initialize it with the logger in `ctx`.
	keepaliveCtx := context.Background()
	if v := logger.LoggerFromContext(ctx); v != nil {
		keepaliveCtx = logger.WithLogger(keepaliveCtx, v)
	}

	go func() {
		t := time.NewTicker(defaultKeepaliveInterval)
		defer t.Stop()
		timeout := func() <-chan time.Time {
			return time.After(defaultKeepaliveTimeout)
		}
		conn.keepalive(keepaliveCtx, t.C, timeout)
	}()
	return conn, nil
}

// connect continuously attempts to connect to a remote server, and returns an
// ssh client if successful, or errs out if the context is canceled.
func connect(ctx context.Context, addr net.Addr, config *ssh.ClientConfig, backoff retry.Backoff) (*Conn, error) {
	startTime := time.Now()

	var client *ssh.Client
	err := retry.Retry(ctx, backoff, func() error {
		logger.Debugf(ctx, "trying to connect to %s...", addr)
		var err error
		client, err = connectToSSH(ctx, addr, config)
		if err != nil {
			return err
		}
		logger.Debugf(ctx, "connected to %s", addr)
		return nil
	}, nil)

	var netErr net.Error
	if errors.As(err, &netErr) && netErr.Timeout() {
		duration := time.Now().Sub(startTime).Truncate(time.Second)
		return nil, ConnectionError{fmt.Errorf("timed out trying to connect to ssh after %v: %w", duration, err)}
	} else if err != nil {
		return nil, ConnectionError{fmt.Errorf("cannot connect to address %q: %w", addr, err)}
	}

	return &Conn{
		Client:       client,
		addr:         addr,
		config:       config,
		shuttingDown: make(chan struct{}),
	}, nil
}

func connectToSSH(ctx context.Context, addr net.Addr, config *ssh.ClientConfig) (*ssh.Client, error) {
	// Update the context with the ssh connection timeout, if specified.
	if config.Timeout != 0 {
		var cancel func()
		ctx, cancel = context.WithTimeout(ctx, config.Timeout)
		defer cancel()
	}

	d := net.Dialer{}
	conn, err := d.DialContext(ctx, "tcp", addr.String())
	if err != nil {
		// DialContext wraps maps context errors to custom non-exported error
		// types, so even if the operation failed due to a context error it
		// might not return a context error. See
		// https://github.com/golang/go/blob/b4652028d48f42506cfd10c1763c6d7e8b22cb7b/src/net/net.go#L420
		// So we convert back to a context error to provide a more consistent
		// interface for callers of this method.
		//
		// There is a potential race condition where the context might be
		// canceled after DialContext exits but before we hit this line, in
		// which case we would actually return the wrong error. But that's
		// probably not a big deal because the fact that the context was
		// canceled implies that we should be giving up on, and ignoring the
		// results of, ongoing operations anyway.
		if ctx.Err() != nil {
			err = ctx.Err()
		}
		return nil, err
	}

	// We made a TCP connection, now establish an SSH connection over it.
	//
	// We can hang if the server accepts a connection but never replies to the
	// ssh handshake. To handle this case, we'll establish the connection in a
	// goroutine, and wait for it to complete or the context to be canceled.
	type result struct {
		client *ssh.Client
		err    error
	}

	ch := make(chan result, 1)

	go func() {
		clientConn, chans, reqs, err := ssh.NewClientConn(conn, addr.String(), config)
		if err != nil {
			if closeErr := conn.Close(); closeErr != nil {
				err = fmt.Errorf("error closing connection: %v; original error: %w", closeErr, err)
			}
			ch <- result{err: err}
			return
		}

		ch <- result{client: ssh.NewClient(clientConn, chans, reqs)}
	}()

	select {
	case r := <-ch:
		return r.client, r.err
	case <-ctx.Done():
		err = ctx.Err()

		if closeErr := conn.Close(); closeErr != nil {
			err = fmt.Errorf("error closing connection: %v; original error: %w", closeErr, err)
		}

		return nil, err
	}
}

func (c *Conn) makeSession(ctx context.Context, stdout io.Writer, stderr io.Writer) (*Session, error) {
	// Temporarily grab the lock and make a copy of the client. This
	// prevents a long running `Run` command from blocking the keepalive
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
		if r.err != nil {
			return nil, fmt.Errorf("failed to start ssh session: %w", r.err)
		}
		return r.session, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// Start a command on the remote device and write STDOUT and STDERR to the
// passed in io.Writers.
func (c *Conn) Start(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) (*Session, error) {
	session, err := c.makeSession(ctx, stdout, stderr)
	if err != nil {
		return nil, err
	}

	logger.Debugf(ctx, "starting over ssh: %s", command)

	if err := session.Start(ctx, command); err != nil {
		session.Close()
		return nil, err
	}
	return session, nil
}

// Run a command to completion on the remote device and write STDOUT and STDERR
// to the passed in io.Writers.
func (c *Conn) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	session, err := c.makeSession(ctx, stdout, stderr)
	if err != nil {
		return err
	}
	defer session.Close()

	logger.Debugf(ctx, "running over ssh: %v", command)

	if err := session.Run(ctx, command); err != nil {
		if ctx.Err() != nil {
			// Don't bother logging the error if the context was canceled.
			return err
		}
		var log string
		var level logger.LogLevel
		switch e := err.(type) {
		case *ssh.ExitError:
			log = fmt.Sprintf("ssh command failed with exit code %d", e.ExitStatus())
			level = logger.DebugLevel
		case *ssh.ExitMissingError:
			log = "ssh command failed with no exit code"
			level = logger.DebugLevel
			err = ConnectionError{err}
		default:
			log = fmt.Sprintf("ssh command failed with error: %v", err)
			level = logger.ErrorLevel
		}
		logger.Logf(ctx, level, "%s: %v", log, command)
		return err
	}
	logger.Debugf(ctx, "successfully ran over ssh: %v", command)
	return nil
}

// Close the ssh client connections.
func (c *Conn) Close() {
	select {
	// Only signal we are shutting down if it hasn't already been closed.
	case <-c.shuttingDown:
	// Notify the keepalive goroutine we are shutting down.
	default:
		close(c.shuttingDown)
	}
	c.disconnect()
}

// RegisterDisconnectListener adds a waiter that gets notified when the ssh
// client is disconnected.
func (c *Conn) RegisterDisconnectListener(ch chan struct{}) {
	c.mu.Lock()
	if c.Client == nil {
		close(ch)
	} else {
		c.disconnectionListeners = append(c.disconnectionListeners, ch)
	}
	c.mu.Unlock()
}

// NewSFTPClient returns an SFTP client that uses the currently underlying
// ssh.Client. The SFTP client will become unresponsive if the ssh connection is
// closed and/or refreshed.
func (c *Conn) NewSFTPClient() (*sftp.Client, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.Client == nil {
		return nil, errors.New("ssh connection is closed, cannot create new SFTP client")
	}
	return sftp.NewClient(c.Client)
}

// disconnect from ssh, and notify anyone waiting for disconnection.
func (c *Conn) disconnect() {
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

// Send periodic keepalives. If we don't do this, then we might not observe
// the server side disconnecting from us.
// A keepalive ping is sent whenever we receive something on the `ticks`
// channel.
// After sending a ping, we call the `timeout` function and wait until either we
// receive a response or we receive something on the channel returned by
// `timeout`.
func (c *Conn) keepalive(ctx context.Context, ticks <-chan time.Time, timeout func() <-chan time.Time) {
	if timeout == nil {
		timeout = func() <-chan time.Time {
			return nil
		}
	}
	for {
		// Sleep until the next poll cycle or until the client is closed.
		select {
		case <-ticks:
		case <-c.shuttingDown:
			return
		}

		c.mu.Lock()
		client := c.Client
		c.mu.Unlock()

		// Exit early if the client's already been shut down.
		if client == nil {
			return
		}

		// SendRequest can actually hang if the server stops responding
		// in between receiving a keepalive and sending a response (see
		// fxb/47698). To protect against this, we'll emit events in a
		// separate goroutine so if we don't get one in the expected
		// time we'll disconnect.
		ch := make(chan error, 1)
		go func() {
			// Try to emit a keepalive message. We use a unique
			// name to distinguish ourselves from the server-side
			// keepalive name to ease debugging. If we get any
			// error, reconnect to the server.
			_, _, err := client.SendRequest(keepaliveOpenSSH, true, nil)
			ch <- err
		}()

		sendTime := time.Now()

		select {
		case <-c.shuttingDown:
			// Ignore the keepalive result if we are shutting down.
			c.disconnect()

		case err := <-ch:
			// disconnect if we hit an error sending a keepalive.
			if err != nil {
				// Ignore a spurious error if we tried to send
				// a keepalive while the connection was closed
				// out from under us.
				select {
				case <-c.shuttingDown:
				default:
					logger.Debugf(
						ctx,
						"error sending keepalive to %s, disconnecting: %s",
						c.addr,
						err,
					)
				}
				c.disconnect()
				return
			}

		case <-timeout():
			timeoutDuration := time.Since(sendTime)
			logger.Debugf(ctx, "ssh keepalive timed out after %.3fs, disconnecting", timeoutDuration.Seconds())
			c.disconnect()
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
