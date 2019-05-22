// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshclient

import (
	"fmt"
	"io"
	"log"
	"net"
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
	disconnectionListeners []*sync.WaitGroup
}

// NewClient creates a new ssh client to the address.
func NewClient(addr string, config *ssh.ClientConfig) (*Client, error) {
	client, conn, err := connect(addr, config)
	if err != nil {
		return nil, err
	}

	c := &Client{
		addr:              addr,
		config:            config,
		client:            client,
		conn:              conn,
		shuttingDown:      false,
		done:              make(chan struct{}),
		keepaliveDuration: 10 * time.Second,
	}
	go c.keepalive()

	return c, nil
}

// Helper to create an ssh client and connection to the address.
func connect(addr string, config *ssh.ClientConfig) (*ssh.Client, net.Conn, error) {
	conn, err := net.DialTimeout("tcp", addr, config.Timeout)
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

// Run a command to completion on the remote device and write STDOUT and STDERR
// to the passed in io.Writers.
func (c *Client) Run(command string, stdout io.Writer, stderr io.Writer) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	log.Printf("running: %s", command)

	if c.client == nil {
		return fmt.Errorf("ssh is disconnected")
	}

	session, err := c.client.NewSession()
	if err != nil {
		return err
	}
	defer session.Close()

	session.Stdout = stdout
	session.Stderr = stderr
	return session.Run(command)
}

// Close the ssh client connections.
func (c *Client) Close() {
	c.shuttingDown = true
	close(c.done)
	c.disconnect()
}

// RegisterDisconnectListener adds a waiter that gets notified when the ssh
// client is disconnected.
func (c *Client) RegisterDisconnectListener(wg *sync.WaitGroup) {
	wg.Add(1)

	c.mu.Lock()
	c.disconnectionListeners = append(c.disconnectionListeners, wg)
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
func (c *Client) reconnect() {
	c.mu.Lock()
	defer c.mu.Unlock()

	// We can exit early if we are shutting down, or we already have a client.
	if c.shuttingDown || c.client != nil {
		return
	}

	log.Printf("attempting to reconnect to %s...", c.addr)

	client, conn, err := connect(c.addr, c.config)
	if err == nil {
		c.client = client
		c.conn = conn
		log.Printf("reconnected to %s", c.addr)
	} else {
		log.Printf("reconnection failed: %s", err)
	}
}

// Send periodic keep-alives. If we don't do this, then we might not observe
// the server side disconnecting from us.
func (c *Client) keepalive() {
	for {
		select {
		case <-time.After(c.keepaliveDuration):
			c.emitKeepalive()
		case <-c.done:
			return
		}
	}
}

func (c *Client) emitKeepalive() {
	// If the client is disconnected from the server, attempt to reconnect.
	// Otherwise, emit a heartbeat.
	if !c.IsConnected() {
		c.reconnect()
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
		c.disconnect()
	}
}
