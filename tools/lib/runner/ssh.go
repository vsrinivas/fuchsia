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
		if ctx.Err() != nil {
			return err
		}
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

	session.Stdout = stdout
	session.Stderr = stderr

	errs := make(chan error)
	go func() {
		if err := session.Run(strings.Join(command, " ")); err != nil {
			errs <- fmt.Errorf("failed to run SSH command: %v", err)
			return
		}
		errs <- nil
	}()

	var runErr error
	select {
	case runErr = <-errs:
	case <-ctx.Done():
		runErr = ctx.Err()
	}

	// A successful ssh.Session.Run() will close the session: no clean-up required.
	if runErr == nil {
		return nil
	}
	logger.Infof(ctx, "attempting to close SSH session due to: %v", runErr)
	if err := session.Signal(ssh.SIGKILL); err != nil {
		logger.Errorf(ctx, "failed to send KILL signal over SSH session: %v", err)
	}
	if err := session.Close(); err != nil {
		logger.Errorf(ctx, "failed to close SSH session: %v", err)
	}
	return runErr
}

// Reconnect closes the underlying connection and attempts to reopen it. The
// method is useful after one has observed that the returned error of Run()
// is of type sshutil.ConnectionError. Also, this can be used
// to recover the runner after having called Close(). If there is an underlying
// connection error, the returned value will unwrap as sshutil.ConnectionError.
func (r *SSHRunner) Reconnect(ctx context.Context) (*ssh.Client, error) {
	raddr := r.client.Conn.RemoteAddr()
	client, err := sshutil.Connect(ctx, raddr, r.config)
	if err != nil {
		return nil, fmt.Errorf("failed to create a new client: %w", err)
	}
	r.Lock()
	r.client.Close()
	r.client = client
	r.Unlock()
	return r.client, nil
}

// ReconnectIfNecessary checks that the connection is alive and attempts to
// reconnect if unresponsive. If there is an underlying connection error, the
// returned value will unwrap as sshutil.ConnectionError.
func (r *SSHRunner) ReconnectIfNecessary(ctx context.Context) (*ssh.Client, error) {
	r.Lock()
	err := sshutil.CheckConnection(r.client)
	r.Unlock()
	if err != nil {
		logger.Errorf(ctx, "SSH connection unresponsive; trying to reconnect: %v", err)
		return r.Reconnect(ctx)
	}
	return r.client, nil
}

// Close closes the underlying client.
func (r *SSHRunner) Close() error {
	r.Lock()
	err := r.client.Close()
	r.Unlock()
	if err != nil {
		return fmt.Errorf("failed to close SSHRunner: %v", err)
	}
	return nil
}
