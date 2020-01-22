// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runner

import (
	"context"
	"fmt"
	"io"
	"strings"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"

	"golang.org/x/crypto/ssh"
)

// SSHRunner runs commands over SSH.
type SSHRunner struct {
	sync.Mutex
	client *ssh.Client
	config *ssh.ClientConfig
}

// NewSSHRunner returns a new SSHRunner given a client and the associated config.
// Passing in the config allows the runner to refresh the underlying connection
// as needed.
func NewSSHRunner(client *ssh.Client, config *ssh.ClientConfig) *SSHRunner {
	return &SSHRunner{
		client: client,
		config: config,
	}
}

// Run executes the given command, returning a *sshutil.ConnectionError if the
// connection has become unresponsive.
func (r *SSHRunner) Run(ctx context.Context, command []string, stdout, stderr io.Writer) error {
	if err := r.run(ctx, command, stdout, stderr); err != nil {
		r.Lock()
		checkErr := sshutil.CheckConnection(r.client)
		r.Unlock()
		if checkErr != nil {
			logger.Errorf(ctx, "ssh client not responsive: %v", err)
			return checkErr
		}
		return err
	}
	return nil
}

func (r *SSHRunner) run(ctx context.Context, command []string, stdout, stderr io.Writer) error {
	r.Lock()
	session, err := r.client.NewSession()
	r.Unlock()
	if err != nil {
		return fmt.Errorf("failed to create an SSH session: %v", err)
	}
	defer func() {
		session.Signal(ssh.SIGKILL)
		session.Close()
	}()

	// TERM=dumb to avoid a loop fetching a cursor position.
	session.Setenv("TERM", "dumb")
	session.Stdout = stdout
	session.Stderr = stderr

	cmd := strings.Join(command, " ")
	if err := session.Start(cmd); err != nil {
		return err
	}

	done := make(chan error)
	go func() {
		done <- session.Wait()
	}()

	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}

// Reconnect closes the underlying connection and attempts to reopen it. The
// method is useful after one has observed that the returned error of Run()
// is of type *sshutil.ConnectionError. Also, this can be used
// to recover the runner after having called Close().
func (r *SSHRunner) Reconnect(ctx context.Context) error {
	raddr := r.client.Conn.RemoteAddr()
	client, err := sshutil.Connect(ctx, raddr, r.config)
	if err != nil {
		return fmt.Errorf("failed to create a new client: %v", err)
	}
	r.Lock()
	r.client.Close()
	r.client = client
	r.Unlock()
	return nil
}

// ReconnectIfNecessary checks that the connection is alive and attempts to
// reconnect if unresponsive.
func (r *SSHRunner) ReconnectIfNecessary(ctx context.Context) error {
	r.Lock()
	err := sshutil.CheckConnection(r.client)
	r.Unlock()
	if err != nil {
		logger.Errorf(ctx, "SSH connection unresponsive; trying to reconnect: %v", err)
		return r.Reconnect(ctx)
	}
	return nil
}

// Close closes the underlying client.
func (r *SSHRunner) Close() error {
	r.Lock()
	err := r.client.Close()
	r.Unlock()
	return err
}
