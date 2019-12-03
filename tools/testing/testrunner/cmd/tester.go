// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"path"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"

	"golang.org/x/crypto/ssh"
)

const (
	// The test output directory to create on the Fuchsia device.
	fuchsiaOutputDir = "/data/infra/testrunner"

	// A conventionally used global request name for checking the status of a client
	// connection to an OpenSSH server.
	keepAliveOpenSSH = "keepalive@openssh.com"

	// Various tools for running tests.
	runtestsName         = "runtests"
	runTestComponentName = "run-test-component"
	runTestSuiteName     = "run-test-suite"

	componentV2Suffix = ".cm"
)

// Tester is executes a Test.
type Tester func(context.Context, build.Test, io.Writer, io.Writer) error

// SubprocessTester executes tests in local subprocesses.
type SubprocessTester struct {
	dir string
	env []string
}

func (t *SubprocessTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) error {
	command := test.Command
	if len(test.Command) == 0 {
		if test.Path == "" {
			return fmt.Errorf("test %q has no `command` or `path` set", test.Name)
		}
		command = []string{test.Path}
	}

	runner := &runner.SubprocessRunner{
		Dir: t.dir,
		Env: t.env,
	}

	return runner.Run(ctx, command, stdout, stderr)
}

// SSHTester executes tests over an SSH connection. It assumes the test.Command
// contains the command line to execute on the remote machine. The caller should Close() the
// tester when finished. Once closed, this object can no longer be used.
type SSHTester struct {
	client    *ssh.Client
	newClient func(ctx context.Context) (*ssh.Client, error)
}

func NewSSHTester(newClient func(context.Context) (*ssh.Client, error)) (*SSHTester, error) {
	client, err := newClient(context.Background())
	if err != nil {
		return nil, err
	}
	return &SSHTester{client: client, newClient: newClient}, nil
}

func (t *SSHTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) error {
	if _, _, err := t.client.Conn.SendRequest(keepAliveOpenSSH, true, nil); err != nil {
		logger.Errorf(ctx, "SSH client not responsive: %v", err)
		client, err := t.newClient(ctx)
		if err != nil {
			return fmt.Errorf("failed to create new SSH client: %v", err)
		}
		t.client.Close()
		t.client = client
	}

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
	useRuntests     bool
}

// NewFuchsiaTester creates a FuchsiaTester object and starts a log_listener process on
// the remote device. The log_listener output can be read from SysLogOutput().
func NewFuchsiaTester(nodename string, sshKey []byte, useRuntests bool) (*FuchsiaTester, error) {
	newClient := func(ctx context.Context) (*ssh.Client, error) {
		config, err := sshutil.DefaultSSHConfig(sshKey)
		if err != nil {
			return nil, fmt.Errorf("failed to create an SSH client config: %v", err)
		}
		client, err := sshutil.ConnectToNode(ctx, nodename, config)
		if err != nil {
			return nil, fmt.Errorf("failed to connect to node %q: %v", nodename, err)
		}
		return client, nil
	}

	delegate, err := NewSSHTester(newClient)
	if err != nil {
		return nil, err
	}
	tester := &FuchsiaTester{
		remoteOutputDir: fuchsiaOutputDir,
		delegate:        delegate,
		useRuntests:     useRuntests,
	}
	return tester, nil
}

func (t *FuchsiaTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) error {
	if len(test.Command) == 0 {
		if !useRuntests && test.PackageURL != "" {
			if strings.HasSuffix(test.PackageURL, componentV2Suffix) {
				test.Command = []string{runTestSuiteName, test.PackageURL}
			} else {
				test.Command = []string{runTestComponentName, test.PackageURL}
			}
		} else {
			name := path.Base(test.Path)
			test.Command = []string{runtestsName, "-t", name, "-o", filepath.Join(t.remoteOutputDir, runtestsName)}
		}
	}
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
