// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

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
)

const (
	// A test output directory within persistent storage.
	dataOutputDir = "/data/infra/testrunner"

	// Various tools for running tests.
	runtestsName         = "runtests"
	runTestComponentName = "run-test-component"
	runTestSuiteName     = "run-test-suite"

	componentV2Suffix = ".cm"
)

// subprocessTester executes tests in local subprocesses.
type subprocessTester struct {
	r *runner.SubprocessRunner
}

// NewSubprocessTester returns a SubprocessTester that can execute tests
// locally with a given working directory and environment.
func newSubprocessTester(dir string, env []string) *subprocessTester {
	return &subprocessTester{
		r: &runner.SubprocessRunner{
			Dir: dir,
			Env: env,
		},
	}
}

func (t *subprocessTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) (runtests.DataSinkMap, error) {
	command := test.Command
	if len(test.Command) == 0 {
		if test.Path == "" {
			return nil, fmt.Errorf("test %q has no `command` or `path` set", test.Name)
		}
		command = []string{test.Path}
	}
	return nil, t.r.Run(ctx, command, stdout, stderr)
}

func (t *subprocessTester) Close() error {
	return nil
}

// fuchsiaSSHTester executes fuchsia tests over an SSH connection.
type fuchsiaSSHTester struct {
	r              *runner.SSHRunner
	copier         *runtests.DataSinkCopier
	useRuntests    bool
	localOutputDir string
}

// newFuchsiaSSHTester returns a fuchsiaSSHTester associated to a fuchsia
// instance of given nodename, the private key paired with an authorized one
// and the directive of whether `runtests` should be used to execute the test.
func newFuchsiaSSHTester(nodename, sshKeyFile, localOutputDir string, useRuntests bool) (*fuchsiaSSHTester, error) {
	key, err := ioutil.ReadFile(sshKeyFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read SSH key file: %v", err)
	}
	config, err := sshutil.DefaultSSHConfig(key)
	if err != nil {
		return nil, fmt.Errorf("failed to create an SSH client config: %v", err)
	}
	client, err := sshutil.ConnectToNode(context.Background(), nodename, config)
	if err != nil {
		return nil, fmt.Errorf("failed to establish an SSH connection: %v", err)
	}
	r := runner.NewSSHRunner(client, config)
	copier, err := runtests.NewDataSinkCopier(client)
	if err != nil {
		return nil, err
	}
	return &fuchsiaSSHTester{
		r:              r,
		copier:         copier,
		useRuntests:    useRuntests,
		localOutputDir: localOutputDir,
	}, nil
}

// Test runs a test over SSH.
func (t *fuchsiaSSHTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) (runtests.DataSinkMap, error) {
	if err := t.r.ReconnectIfNecessary(ctx); err != nil {
		return nil, fmt.Errorf("failed to restablish SSH connection: %v", err)
	}

	setCommand(&test, t.useRuntests, dataOutputDir)
	testErr := t.r.Run(ctx, test.Command, stdout, stderr)

	var copyErr error
	var sinks runtests.DataSinkMap
	if t.useRuntests {
		if sinks, copyErr = t.copier.Copy(dataOutputDir, t.localOutputDir); copyErr != nil {
			logger.Errorf(ctx, "failed to copy data sinks off target for test %q: %v", test.Name, copyErr)
		}
	}

	if testErr == nil {
		return sinks, copyErr
	}
	return sinks, testErr
}

// Close terminates the underlying SSH connection. The object is no longer
// usable after calling this method.
func (t *fuchsiaSSHTester) Close() error {
	if err := t.copier.Close(); err != nil {
		t.r.Close()
		return err
	}
	return t.r.Close()
}

func setCommand(test *build.Test, useRuntests bool, remoteOutputDir string) {
	if len(test.Command) > 0 {
		return
	}

	if useRuntests {
		name := path.Base(test.Path)
		dir := path.Dir(test.Path)
		test.Command = []string{runtestsName, "-t", name, dir, "-o", remoteOutputDir}
		return
	} else if test.PackageURL != "" {
		if strings.HasSuffix(test.PackageURL, componentV2Suffix) {
			test.Command = []string{runTestSuiteName, test.PackageURL}
		} else {
			test.Command = []string{runTestComponentName, test.PackageURL}
		}
	} else {
		test.Command = []string{test.Path}
	}
}
