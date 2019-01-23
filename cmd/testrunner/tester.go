// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"io"

	"fuchsia.googlesource.com/tools/testrunner"
	"fuchsia.googlesource.com/tools/testsharder"
	"golang.org/x/crypto/ssh"
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
// contains the command line to execute on the remote machine.
type SSHTester struct {
	client *ssh.Client
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

// This is a hack. We have to run Fuchsia tests using `runtests` on the remote device
// because there are many ways to execute Fuchsia tests and runtests already does this
// correctly. This wrapper around SSHTester is meant to keep SSHTester free of OS-specific
// behavior. Later we'll delete this and use SSHTester directly.
type FuchsiaTester struct {
	remoteOutputDir string
	delegate        *SSHTester
}

func (t *FuchsiaTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer) error {
	test.Command = []string{"runtests", "-t", test.Location, "-o", t.remoteOutputDir + "runtests"}
	return t.delegate.Test(ctx, test, stdout, stderr)
}
