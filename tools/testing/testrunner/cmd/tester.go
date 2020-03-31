// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"path"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"golang.org/x/crypto/ssh"
)

const (
	// A test output directory within persistent storage.
	dataOutputDir = "/data/infra/testrunner"

	// Various tools for running tests.
	runtestsName         = "runtests"
	runTestComponentName = "run-test-component"
	runTestSuiteName     = "run-test-suite"

	componentV2Suffix = ".cm"

	// Returned by both run-test-component and run-test-suite to indicate the
	// test timed out.
	timeoutExitCode = 21
)

type timeoutError struct {
	timeout time.Duration
}

func (e *timeoutError) Error() string {
	return fmt.Sprintf("test killed because timeout reached (%v)", e.timeout)
}

// For testability
type cmdRunner interface {
	Run(ctx context.Context, command []string, stdout, stderr io.Writer) error
}

// For testability
type sshRunner interface {
	Close() error
	ReconnectIfNecessary(ctx context.Context) (*ssh.Client, error)
	Run(ctx context.Context, command []string, stdout, stderr io.Writer) error
}

// For testability
type dataSinkCopier interface {
	Copy(remoteDir, localDir string) (runtests.DataSinkMap, error)
	Close() error
}

// subprocessTester executes tests in local subprocesses.
type subprocessTester struct {
	r              cmdRunner
	perTestTimeout time.Duration
}

// NewSubprocessTester returns a SubprocessTester that can execute tests
// locally with a given working directory and environment.
func newSubprocessTester(dir string, env []string, perTestTimeout time.Duration) *subprocessTester {
	return &subprocessTester{
		r: &runner.SubprocessRunner{
			Dir: dir,
			Env: env,
		},
		perTestTimeout: perTestTimeout,
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
	if t.perTestTimeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, t.perTestTimeout)
		defer cancel()
	}
	err := t.r.Run(ctx, command, stdout, stderr)
	if err == context.DeadlineExceeded {
		return nil, &timeoutError{t.perTestTimeout}
	}
	return nil, err
}

func (t *subprocessTester) Close() error {
	return nil
}

// fuchsiaSSHTester executes fuchsia tests over an SSH connection.
type fuchsiaSSHTester struct {
	r                           sshRunner
	client                      *ssh.Client
	copier                      dataSinkCopier
	useRuntests                 bool
	localOutputDir              string
	perTestTimeout              time.Duration
	connectionErrorRetryBackoff retry.Backoff
}

// newFuchsiaSSHTester returns a fuchsiaSSHTester associated to a fuchsia
// instance of given nodename, the private key paired with an authorized one
// and the directive of whether `runtests` should be used to execute the test.
func newFuchsiaSSHTester(ctx context.Context, nodename, sshKeyFile, localOutputDir string, useRuntests bool, perTestTimeout time.Duration) (*fuchsiaSSHTester, error) {
	key, err := ioutil.ReadFile(sshKeyFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read SSH key file: %v", err)
	}
	config, err := sshutil.DefaultSSHConfig(key)
	if err != nil {
		return nil, fmt.Errorf("failed to create an SSH client config: %v", err)
	}
	client, err := sshutil.ConnectToNode(ctx, nodename, config)
	if err != nil {
		return nil, fmt.Errorf("failed to establish an SSH connection: %v", err)
	}
	r := runner.NewSSHRunner(client, config)
	copier, err := runtests.NewDataSinkCopier(client)
	if err != nil {
		return nil, err
	}
	return &fuchsiaSSHTester{
		r:                           r,
		client:                      client,
		copier:                      copier,
		useRuntests:                 useRuntests,
		localOutputDir:              localOutputDir,
		perTestTimeout:              perTestTimeout,
		connectionErrorRetryBackoff: retry.NewConstantBackoff(time.Second),
	}, nil
}

func (t *fuchsiaSSHTester) reconnectIfNecessary(ctx context.Context) error {
	if client, err := t.r.ReconnectIfNecessary(ctx); err != nil {
		return fmt.Errorf("failed to restablish SSH connection: %w", err)
	} else if client != t.client {
		// Create new DataSinkCopier with new client.
		t.client = client
		if err := t.copier.Close(); err != nil {
			logger.Errorf(ctx, "failed to close data sink copier: %v", err)
		}
		t.copier, err = runtests.NewDataSinkCopier(t.client)
		if err != nil {
			return fmt.Errorf("failed to create new data sink copier: %w", err)
		}
	}
	return nil
}

func (t *fuchsiaSSHTester) isTimeoutError(test build.Test, err error) bool {
	if t.perTestTimeout <= 0 || (
	// We only know how to interpret the exit codes of these test runners.
	test.Command[0] != runTestComponentName && test.Command[0] != runTestSuiteName) {
		return false
	}
	if exitErr, ok := err.(*ssh.ExitError); ok {
		return exitErr.Waitmsg.ExitStatus() == timeoutExitCode
	}
	return false
}

// Test runs a test over SSH.
func (t *fuchsiaSSHTester) Test(ctx context.Context, test build.Test, stdout io.Writer, stderr io.Writer) (runtests.DataSinkMap, error) {
	setCommand(&test, t.useRuntests, dataOutputDir, t.perTestTimeout)
	var testErr error
	const maxReconnectAttempts = 2
	retry.Retry(ctx, retry.WithMaxRetries(t.connectionErrorRetryBackoff, maxReconnectAttempts), func() error {
		testErr = t.r.Run(ctx, test.Command, stdout, stderr)
		if errors.Is(testErr, sshutil.ConnectionError) {
			logger.Errorf(ctx, "attempting to reconnect over SSH after error: %v", testErr)
			if err := t.reconnectIfNecessary(ctx); err != nil {
				logger.Errorf(ctx, "failed to reconnect over SSH: %v", err)
				// If we fail to reconnect, continuing is likely hopeless.
				return nil
			}
			// Return non-ConnectionError because code in main.go will exit early if
			// it sees that. Since reconnection succeeded, we don't want that.
			// TODO(garymm): Clean this up; have main.go do its own connection recovery between tests.
			testErr = fmt.Errorf("%v", testErr)
			return testErr
		}
		// Not a connection error -> test failed -> break retry loop.
		return nil
	}, nil)

	if errors.Is(testErr, sshutil.ConnectionError) {
		return nil, testErr
	}

	if t.isTimeoutError(test, testErr) {
		testErr = &timeoutError{t.perTestTimeout}
	}

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

func setCommand(test *build.Test, useRuntests bool, remoteOutputDir string, timeout time.Duration) {
	if len(test.Command) > 0 {
		return
	}

	if useRuntests {
		if test.PackageURL != "" {
			test.Command = []string{runtestsName, "-t", test.PackageURL, "-o", remoteOutputDir}
		} else {
			name := path.Base(test.Path)
			dir := path.Dir(test.Path)
			test.Command = []string{runtestsName, "-t", name, dir, "-o", remoteOutputDir}
		}
		if timeout > 0 {
			test.Command = append(test.Command, "-i", fmt.Sprintf("%d", int64(timeout.Seconds())))
		}
	} else if test.PackageURL != "" {
		if strings.HasSuffix(test.PackageURL, componentV2Suffix) {
			test.Command = []string{runTestSuiteName}
			// TODO(fxbug.dev/49262): Once fixed, combine
			// timeout flag setting for v1 and v2.
			if timeout > 0 {
				test.Command = append(test.Command, "--timeout", fmt.Sprintf("%d", int64(timeout.Seconds())))
			}
		} else {
			test.Command = []string{runTestComponentName}
			if timeout > 0 {
				test.Command = append(test.Command, fmt.Sprintf("--timeout=%d", int64(timeout.Seconds())))
			}
		}
		test.Command = append(test.Command, test.PackageURL)
	} else {
		test.Command = []string{test.Path}
		if timeout > 0 {
			logger.Warningf(
				context.Background(),
				"timeout specified but will not be enforced because the test is being run directly (not by a runner such as %s)",
				runTestComponentName)
		}
	}
}
