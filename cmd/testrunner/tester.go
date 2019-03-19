// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"path"

	"fuchsia.googlesource.com/tools/runner"
	"fuchsia.googlesource.com/tools/sshutil"
	"fuchsia.googlesource.com/tools/testsharder"
	"golang.org/x/crypto/ssh"
)

const (
	// The test output directory to create on the Fuchsia device.
	fuchsiaOutputDir = "/data/infra/testrunner"
)

// Tester is executes a Test.
type Tester func(context.Context, testsharder.Test, io.Writer, io.Writer) error

// SubprocessTester executes tests in local subprocesses.
type SubprocessTester struct {
	wd  string
	env []string
}

func (t *SubprocessTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer) error {
	command := test.Command
	if len(test.Command) == 0 {
		if test.Path == "" {
			return fmt.Errorf("test %q has no `command` or `path` set", test.Name)
		}
		command = []string{test.Path}
	}

	runner := &runner.SubprocessRunner{
		WD:  t.wd,
		Env: t.env,
	}

	return runner.Run(ctx, command, stdout, stderr)
}

// SSHTester executes tests over an SSH connection. It assumes the test.Command
// contains the command line to execute on the remote machine. The caller should Close() the
// tester when finished. Once closed, this object can no longer be used.
type SSHTester struct {
	client *ssh.Client
}

func NewSSHTester(nodename string, sshKey []byte) (*SSHTester, error) {
	config, err := sshutil.DefaultSSHConfig(sshKey)
	if err != nil {
		return nil, fmt.Errorf("failed to create an SSH client config: %v", err)
	}
	client, err := sshutil.ConnectToNode(context.Background(), nodename, config)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to node %q: %v", nodename, err)
	}
	return &SSHTester{client: client}, nil
}

func (t *SSHTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer) error {
	session, err := t.client.NewSession()
	if err != nil {
		return err
	}
	defer session.Close()

	runner := &runner.SSHRunner{Session: session}
	return runner.Run(ctx, test.Command, stdout, stderr)
}

// Close stops this SSHTester.  The underlying SSH connection is terminated.  The object
// is no longer usable after calling this method.
func (t *SSHTester) Close() error {
	return t.client.Close()
}

// FuchsiaTester executes tests on remote Fuchsia devices. The caller should Close() the
// tester when finished. Once closed, this object can no longer be used.
//
// This is a hack. We have to run Fuchsia tests using `runtests` on the remote device
// because there are many ways to execute Fuchsia tests and runtests already does this
// correctly. This wrapper around SSHTester is meant to keep SSHTester free of OS-specific
// behavior. Later we'll delete this and use SSHTester directly.
type FuchsiaTester struct {
	remoteOutputDir string
	delegate        *SSHTester
}

// NewFuchsiaTester creates a FuchsiaTester object and starts a log_listener process on
// the remote device. The log_listener output can be read from SysLogOutput().
func NewFuchsiaTester(nodename string, sshKey []byte) (*FuchsiaTester, error) {
	delegate, err := NewSSHTester(nodename, sshKey)
	if err != nil {
		return nil, err
	}
	tester := &FuchsiaTester{
		remoteOutputDir: fuchsiaOutputDir,
		delegate:        delegate,
	}
	return tester, nil
}

func (t *FuchsiaTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer) error {
	name := path.Base(test.Path)
	test.Command = []string{"runtests", "-t", name, "-o", t.remoteOutputDir + "runtests"}
	return t.delegate.Test(ctx, test, stdout, stderr)
}

// Close stops this FuchsiaTester.  The remote log_listener process is terminated along
// with the underlying SSH connection.  The object is no longer usable after calling this
// method.
func (t *FuchsiaTester) Close() error {
	if err := t.delegate.Close(); err != nil {
		return fmt.Errorf("failed to close delegate ssh runner: %v", err)
	}
	return nil
}
