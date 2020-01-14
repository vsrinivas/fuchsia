// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"path"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"

	"golang.org/x/crypto/ssh"
)

const (
	// The test output directory to create on the Fuchsia device.
	fuchsiaOutputDir = "/data/infra/testrunner"

	// Various tools for running tests.
	runtestsName         = "runtests"
	runTestComponentName = "run-test-component"
	runTestSuiteName     = "run-test-suite"

	componentV2Suffix = ".cm"
)

// Tester executes a Test.
type Tester func(context.Context, build.Test, io.Writer, io.Writer) (runtests.DataSinkMap, error)

// DataSinkCopier copies data sinks from a remote host after a runtests
// invocation.
type dataSinkCopier func(string, string) (runtests.DataSinkMap, error)

// SubprocessTester executes tests in local subprocesses.
type SubprocessTester struct {
	dir string
	env []string
}

func (t *SubprocessTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) (runtests.DataSinkMap, error) {
	command := test.Command
	if len(test.Command) == 0 {
		if test.Path == "" {
			return nil, fmt.Errorf("test %q has no `command` or `path` set", test.Name)
		}
		command = []string{test.Path}
	}

	runner := &runner.SubprocessRunner{
		Dir: t.dir,
		Env: t.env,
	}

	return nil, runner.Run(ctx, command, stdout, stderr)
}

// SSHTester executes tests over an SSH connection. It assumes the test.Command
// contains the command line to execute on the remote machine. The caller should Close() the
// tester when finished. Once closed, this object can no longer be used.
type SSHTester struct {
	r               *runner.SSHRunner
	remoteOutputDir string
	copier          *runtests.DataSinkCopier
}

func NewSSHTester(client *ssh.Client, config *ssh.ClientConfig) (*SSHTester, error) {
	r := runner.NewSSHRunner(client, config)
	copier, err := runtests.NewDataSinkCopier(client)
	if err != nil {
		return nil, err
	}
	return &SSHTester{
		r:               r,
		remoteOutputDir: fuchsiaOutputDir,
		copier:          copier,
	}, nil
}

func (t *SSHTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) (runtests.DataSinkMap, error) {
	if err := t.r.ReconnectIfNecessary(ctx); err != nil {
		return nil, fmt.Errorf("failed to restablish SSH connection: %v", err)
	}

	testErr := t.r.Run(ctx, test.Command, stdout, stderr)

	var copyErr error
	var sinks runtests.DataSinkMap
	if test.Command[0] == runtestsName {
		var localOutputDir string
		if localOutputDir, copyErr = ioutil.TempDir("", "sinks"); copyErr == nil {
			sinks, copyErr = t.copier.Copy(t.remoteOutputDir, localOutputDir)
		}
	}
	if copyErr != nil {
		logger.Errorf(ctx, "failed to copy data sinks off target for test %q: %v", test.Name, copyErr)
	}

	if testErr == nil {
		return sinks, copyErr
	}
	return sinks, testErr
}

// Close stops this SSHTester. The underlying SSH connection is terminated. The
// object is no longer usable after calling this method.
func (t *SSHTester) Close() error {
	// Note that t.copier's underlying connection is owned by the SSH runner.
	return t.r.Close()
}

// FuchsiaTester executes tests on remote Fuchsia devices. The caller should Close() the
// tester when finished. Once closed, this object can no longer be used.
//
// This is a hack. We have to run Fuchsia tests using `runtests` on the remote device
// because there are many ways to execute Fuchsia tests and runtests already does this
// correctly. This wrapper around SSHTester is meant to keep SSHTester free of OS-specific
// behavior. Later we'll delete this and use SSHTester directly.
type FuchsiaTester struct {
	delegate    *SSHTester
	useRuntests bool
}

// NewFuchsiaTester creates a FuchsiaTester object and starts a log_listener process on
// the remote device. The log_listener output can be read from SysLogOutput().
func NewFuchsiaTester(nodename string, sshKey []byte, useRuntests bool) (*FuchsiaTester, error) {
	config, err := sshutil.DefaultSSHConfig(sshKey)
	if err != nil {
		return nil, fmt.Errorf("failed to create an SSH client config: %v", err)
	}
	client, err := sshutil.ConnectToNode(context.Background(), nodename, config)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to node %q: %v", nodename, err)
	}

	delegate, err := NewSSHTester(client, config)
	if err != nil {
		return nil, err
	}
	tester := &FuchsiaTester{
		delegate:    delegate,
		useRuntests: useRuntests,
	}
	return tester, nil
}

func (t *FuchsiaTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) (dataSinks runtests.DataSinkMap, err error) {
	if len(test.Command) == 0 {
		if !t.useRuntests && test.PackageURL != "" {
			if strings.HasSuffix(test.PackageURL, componentV2Suffix) {
				test.Command = []string{runTestSuiteName, test.PackageURL}
			} else {
				test.Command = []string{runTestComponentName, test.PackageURL}
			}
		} else {
			name := path.Base(test.Path)
			test.Command = []string{runtestsName, "-t", name, "-o", t.delegate.remoteOutputDir}
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
