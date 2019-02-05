// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"path"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/testrunner"
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
	var command []string
	if len(test.Location) > 0 {
		command = []string{test.Location}
	} else {
		command = test.Command
	}

	runner := &testrunner.SubprocessRunner{
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

func NewSSHTester(devCtx botanist.DeviceContext) (*SSHTester, error) {
	client, err := sshIntoNode(devCtx.Nodename, devCtx.SSHKey)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to node %q: %v", devCtx.Nodename, err)
	}
	return &SSHTester{client: client}, nil
}

func (t *SSHTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer) error {
	session, err := t.client.NewSession()
	if err != nil {
		return err
	}
	defer session.Close()

	runner := &testrunner.SSHRunner{Session: session}
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
	remoteSyslog    *remoteSyslog
}

// NewFuchsiaTester creates a FuchsiaTester object and starts a log_listener process on
// the remote device. The log_listener output can be read from SysLogOutput().
func NewFuchsiaTester(devCtx botanist.DeviceContext) (*FuchsiaTester, error) {
	delegate, err := NewSSHTester(devCtx)
	if err != nil {
		return nil, err
	}

	tester := &FuchsiaTester{
		remoteOutputDir: fuchsiaOutputDir,
		delegate:        delegate,
	}

	tester.remoteSyslog, err = newRemoteSyslog(context.Background(), delegate.client)
	if err != nil {
		return nil, fmt.Errorf("failed to start remote log_listener process: %v", err)
	}

	return tester, nil
}

func (t *FuchsiaTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer) error {
	name := path.Base(test.Location)
	test.Command = []string{"runtests", "-t", name, "-o", t.remoteOutputDir + "runtests"}
	return t.delegate.Test(ctx, test, stdout, stderr)
}

// SysLogOutput returns the stdout and stderr streams of the remote log_listener process.
func (t *FuchsiaTester) SyslogOutput() (stdout, stderr *bytes.Reader) {
	return t.remoteSyslog.stdout(), t.remoteSyslog.stderr()
}

// Close stops this FuchsiaTester.  The remote log_listener process is terminated along
// with the underlying SSH connection.  The object is no longer usable after calling this
// method.
func (t *FuchsiaTester) Close() error {
	if err := t.remoteSyslog.stop(); err != nil {
		return fmt.Errorf("failed to terminate remote log_listener: %v", err)
	}
	if err := t.delegate.Close(); err != nil {
		return fmt.Errorf("failed to close delegate ssh runner: %v", err)
	}
	return nil
}

// A handle to a remote log_listener process on some Fuchsia device.
type remoteSyslog struct {
	stdoutBuf *bytes.Buffer
	stderrBuf *bytes.Buffer
	session   *ssh.Session
}

// Starts a remote process on the Fuchsia device to run log_listener. The output of the
// process is written to the given stdout and stderr streams. The process will terminate
// when the FuchsiaTester is Close().
func newRemoteSyslog(ctx context.Context, client *ssh.Client) (*remoteSyslog, error) {
	session, err := client.NewSession()
	if err != nil {
		return nil, err
	}

	sl := &remoteSyslog{
		session:   session,
		stdoutBuf: new(bytes.Buffer),
		stderrBuf: new(bytes.Buffer),
	}

	runner := &testrunner.SSHRunner{Session: session}
	go runner.Run(ctx, []string{"bin/log_listener"}, sl.stdoutBuf, sl.stderrBuf)
	return sl, nil
}

func (sl *remoteSyslog) stdout() *bytes.Reader {
	return bytes.NewReader(sl.stdoutBuf.Bytes())
}

func (sl *remoteSyslog) stderr() *bytes.Reader {
	return bytes.NewReader(sl.stderrBuf.Bytes())
}

func (sl *remoteSyslog) stop() error {
	return sl.session.Close()
}
