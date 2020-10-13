// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
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

	// Printed to the serial console when ready to accept user input.
	serialConsoleCursor = "\n$"
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
var newRunner = func(dir string, env []string) cmdRunner {
	return &runner.SubprocessRunner{Dir: dir, Env: env}
}

// For testability
type sshClient interface {
	Close()
	Reconnect(ctx context.Context) error
	Run(ctx context.Context, command []string, stdout, stderr io.Writer) error
}

// For testability
type dataSinkCopier interface {
	GetReference() (runtests.DataSinkReference, error)
	Copy(sinks []runtests.DataSinkReference, localDir string) (runtests.DataSinkMap, error)
	Reconnect() error
	Close() error
}

// subprocessTester executes tests in local subprocesses.
type subprocessTester struct {
	env            []string
	dir            string
	perTestTimeout time.Duration
}

// NewSubprocessTester returns a SubprocessTester that can execute tests
// locally with a given working directory and environment.
func newSubprocessTester(dir string, env []string, perTestTimeout time.Duration) *subprocessTester {
	return &subprocessTester{
		dir:            dir,
		env:            env,
		perTestTimeout: perTestTimeout,
	}
}

func (t *subprocessTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer, outDir string) (runtests.DataSinkReference, error) {
	if test.Path == "" {
		return nil, fmt.Errorf("test %q has no `path` set", test.Name)
	}
	// Some tests read testOutDirEnvKey so ensure they get their own output dir.
	if err := os.MkdirAll(outDir, 0770); err != nil {
		return nil, err
	}
	r := newRunner(t.dir,
		append(t.env, fmt.Sprintf("%s=%s", testOutDirEnvKey, outDir)))
	if t.perTestTimeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, t.perTestTimeout)
		defer cancel()
	}
	err := r.Run(ctx, []string{test.Path}, stdout, stderr)
	if err == context.DeadlineExceeded {
		return nil, &timeoutError{t.perTestTimeout}
	}
	return nil, err
}

func (t *subprocessTester) CopySinks(_ context.Context, _ []runtests.DataSinkReference) error {
	return nil
}

func (t *subprocessTester) RunSnapshot(_ context.Context, _ string) error {
	return nil
}

func (t *subprocessTester) Close() error {
	return nil
}

// fuchsiaSSHTester executes fuchsia tests over an SSH connection.
type fuchsiaSSHTester struct {
	client                      sshClient
	copier                      dataSinkCopier
	useRuntests                 bool
	localOutputDir              string
	perTestTimeout              time.Duration
	connectionErrorRetryBackoff retry.Backoff
	serialSocketPath            string
}

// newFuchsiaSSHTester returns a fuchsiaSSHTester associated to a fuchsia
// instance of given nodename, the private key paired with an authorized one
// and the directive of whether `runtests` should be used to execute the test.
func newFuchsiaSSHTester(ctx context.Context, addr net.IPAddr, sshKeyFile, localOutputDir, serialSocketPath string, useRuntests bool, perTestTimeout time.Duration) (*fuchsiaSSHTester, error) {
	key, err := ioutil.ReadFile(sshKeyFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read SSH key file: %w", err)
	}
	config, err := sshutil.DefaultSSHConfig(key)
	if err != nil {
		return nil, fmt.Errorf("failed to create an SSH client config: %w", err)
	}

	client, err := sshutil.NewClient(ctx, &net.TCPAddr{
		IP:   addr.IP,
		Port: sshutil.SSHPort,
		Zone: addr.Zone,
	}, config, sshutil.DefaultConnectBackoff())
	if err != nil {
		return nil, fmt.Errorf("failed to establish an SSH connection: %w", err)
	}
	copier, err := runtests.NewDataSinkCopier(client, dataOutputDir)
	if err != nil {
		return nil, err
	}
	return &fuchsiaSSHTester{
		client:                      client,
		copier:                      copier,
		useRuntests:                 useRuntests,
		localOutputDir:              localOutputDir,
		perTestTimeout:              perTestTimeout,
		connectionErrorRetryBackoff: retry.NewConstantBackoff(time.Second),
		serialSocketPath:            serialSocketPath,
	}, nil
}

func asSerialCmd(cmd []string) string {
	// The UART kernel driver expects a command to be followed by \r\n.
	return fmt.Sprintf("%s\r\n", strings.Join(cmd, " "))
}

var serialDiagnosticCmds = [][]string{{"k", "threadload"}, {"k", "threadq"}}
var diagnosticToggleDuration = 5 * time.Second

func (t *fuchsiaSSHTester) runSerialDiagnostics(ctx context.Context) error {
	if t.serialSocketPath == "" {
		return fmt.Errorf("serialSocketPath not set")
	}
	logger.Debugf(ctx, "attempting to run diagnostics over serial")
	socket, err := newSerialSocket(ctx, t.serialSocketPath)
	if err != nil {
		return fmt.Errorf("newSerialSocket failed: %v", err)
	}
	defer socket.Close()
	for _, cmd := range serialDiagnosticCmds {
		logger.Debugf(ctx, "to enable diagnostics, running over serial: %v", cmd)
		if _, err := io.WriteString(socket, asSerialCmd(cmd)); err != nil {
			return fmt.Errorf("failed to write to serial socket: %v", err)
		}
		logger.Debugf(ctx, "sleeping for %v", diagnosticToggleDuration)
		time.Sleep(diagnosticToggleDuration)
		logger.Debugf(ctx, "to disable diagnostics, running over serial: %v", cmd)
		if _, err := io.WriteString(socket, asSerialCmd(cmd)); err != nil {
			return fmt.Errorf("failed to write to serial socket: %v", err)
		}
	}
	return nil
}

func (t *fuchsiaSSHTester) reconnect(ctx context.Context) error {
	if err := t.client.Reconnect(ctx); err != nil {
		return fmt.Errorf("failed to reestablish SSH connection: %w", err)
	}
	if err := t.copier.Reconnect(); err != nil {
		return fmt.Errorf("failed to reconnect data sink copier: %w", err)
	}
	return nil
}

func (t *fuchsiaSSHTester) isTimeoutError(test testsharder.Test, err error) bool {
	if t.perTestTimeout <= 0 {
		return false
	}
	if exitErr, ok := err.(*ssh.ExitError); ok {
		return exitErr.Waitmsg.ExitStatus() == timeoutExitCode
	}
	return false
}

func (t *fuchsiaSSHTester) runSSHCommandWithRetry(ctx context.Context, command []string, stdout, stderr io.Writer) error {
	var cmdErr error
	const maxReconnectAttempts = 3
	retry.Retry(ctx, retry.WithMaxAttempts(t.connectionErrorRetryBackoff, maxReconnectAttempts), func() error {
		cmdErr = t.client.Run(ctx, command, stdout, stderr)
		if sshutil.IsConnectionError(cmdErr) {
			logger.Errorf(ctx, "attempting to reconnect over SSH after error: %v", cmdErr)
			if err := t.reconnect(ctx); err != nil {
				logger.Errorf(ctx, "%s: %v", constants.FailedToReconnectMsg, err)
				// If we fail to reconnect, continuing is likely hopeless.
				return nil
			}
			// Return non-ConnectionError because code in main.go will exit early if
			// it sees that. Since reconnection succeeded, we don't want that.
			// TODO(garymm): Clean this up; have main.go do its own connection recovery between tests.
			cmdErr = fmt.Errorf("%v", cmdErr)
			return cmdErr
		}
		// Not a connection error -> command passed or failed -> break retry loop.
		return nil
	}, nil)
	return cmdErr
}

// Return this error when the test is skipped.
type TestSkippedError struct{}

func (e *TestSkippedError) Error() string {
	return "test skipped"
}

func isTestSkippedErr(err error) bool {
	_, ok := err.(*TestSkippedError)
	return ok
}

// Test runs a test over SSH.
func (t *fuchsiaSSHTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer, _ string) (runtests.DataSinkReference, error) {
	// runtests doesn't support v2 coverage data. fxbug.dev/61180 tracks an alternative for v2.
	if t.useRuntests && strings.HasSuffix(test.PackageURL, componentV2Suffix) {
		return nil, &TestSkippedError{}
	}
	command, err := commandForTest(&test, t.useRuntests, dataOutputDir, t.perTestTimeout)
	if err != nil {
		return nil, err
	}
	testErr := t.runSSHCommandWithRetry(ctx, command, stdout, stderr)

	if sshutil.IsConnectionError(testErr) {
		if err := t.runSerialDiagnostics(ctx); err != nil {
			logger.Warningf(ctx, "failed to run serial diagnostics: %v", err)
		}
		return nil, testErr
	}

	if t.isTimeoutError(test, testErr) {
		testErr = &timeoutError{t.perTestTimeout}
	}

	var sinkErr error
	var sinks runtests.DataSinkReference
	if t.useRuntests {
		startTime := time.Now()
		if sinks, sinkErr = t.copier.GetReference(); sinkErr != nil {
			logger.Errorf(ctx, "failed to determine data sinks for test %q: %v", test.Name, sinkErr)
		}
		duration := time.Now().Sub(startTime)
		if sinks.Size() > 0 {
			logger.Debugf(ctx, "%d data sinks found in %v", sinks.Size(), duration)
		}
	}

	if testErr == nil {
		return sinks, sinkErr
	}
	return sinks, testErr
}

func (t *fuchsiaSSHTester) CopySinks(ctx context.Context, sinks []runtests.DataSinkReference) error {
	startTime := time.Now()
	sinkMap, err := t.copier.Copy(sinks, t.localOutputDir)
	if err != nil {
		return fmt.Errorf("failed to copy data sinks off target: %v", err)
	}
	copyDuration := time.Now().Sub(startTime)
	numSinks := runtests.DataSinkReference(sinkMap).Size()
	if numSinks > 0 {
		logger.Debugf(ctx, "copied %d data sinks in %v", numSinks, copyDuration)
	}
	return nil
}

// RunSnapshot runs `snapshot` on the device.
func (t *fuchsiaSSHTester) RunSnapshot(ctx context.Context, snapshotFile string) error {
	if snapshotFile == "" {
		return nil
	}
	snapshotOutFile, err := osmisc.CreateFile(filepath.Join(t.localOutputDir, snapshotFile))
	if err != nil {
		return fmt.Errorf("failed to create snapshot output file: %w", err)
	}
	defer snapshotOutFile.Close()
	startTime := time.Now()
	err = t.runSSHCommandWithRetry(ctx, []string{"/bin/snapshot"}, snapshotOutFile, os.Stderr)
	if err != nil {
		logger.Errorf(ctx, "%s: %v", constants.FailedToRunSnapshotMsg, err)
	}
	logger.Debugf(ctx, "ran snapshot in %v", time.Now().Sub(startTime))
	return err
}

// Close terminates the underlying SSH connection. The object is no longer
// usable after calling this method.
func (t *fuchsiaSSHTester) Close() error {
	defer t.client.Close()
	return t.copier.Close()
}

// FuchsiaSerialTester executes fuchsia tests over serial.
type fuchsiaSerialTester struct {
	socket         io.ReadWriteCloser
	perTestTimeout time.Duration
	localOutputDir string
}

func newSerialSocket(ctx context.Context, path string) (io.ReadWriteCloser, error) {
	socket, err := net.Dial("unix", path)
	if err != nil {
		return nil, fmt.Errorf("failed to open serial socket connection: %v", err)
	}
	// Trigger a new cursor print by sending a newline. This may do nothing if the
	// system was not ready to process input, but in that case it will print a
	// new cursor anyways when it is ready to receive input.
	io.WriteString(socket, asSerialCmd([]string{}))
	// Look for the cursor, which should indicate that the console is ready for input.
	m := iomisc.NewSequenceMatchingReader(socket, serialConsoleCursor)
	if _, err = iomisc.ReadUntilMatch(ctx, m, nil); err != nil {
		return nil, fmt.Errorf("failed to find cursor: %v", err)
	}
	return socket, nil
}

func newFuchsiaSerialTester(ctx context.Context, serialSocketPath string, perTestTimeout time.Duration) (*fuchsiaSerialTester, error) {
	socket, err := newSerialSocket(ctx, serialSocketPath)
	if err != nil {
		return nil, err
	}

	return &fuchsiaSerialTester{
		socket:         socket,
		perTestTimeout: perTestTimeout,
	}, nil
}

func (t *fuchsiaSerialTester) Test(ctx context.Context, test testsharder.Test, _, _ io.Writer, _ string) (runtests.DataSinkReference, error) {
	command, err := commandForTest(&test, true, dataOutputDir, t.perTestTimeout)
	if err != nil {
		return nil, err
	}
	cmd := asSerialCmd(command)
	logger.Debugf(ctx, "starting: %v", command)
	if _, err := io.WriteString(t.socket, cmd); err != nil {
		return nil, fmt.Errorf("failed to write to serial socket: %v", err)
	}

	success, err := runtests.TestPassed(ctx, t.socket, test.Name)

	if err != nil {
		return nil, err
	} else if !success {
		return nil, fmt.Errorf("test failed")
	}
	return nil, nil
}

func (t *fuchsiaSerialTester) CopySinks(_ context.Context, _ []runtests.DataSinkReference) error {
	return nil
}

func (t *fuchsiaSerialTester) RunSnapshot(_ context.Context, _ string) error {
	return nil
}

// Close terminates the underlying Serial socket connection. The object is no
// longer usable after calling this method.
func (t *fuchsiaSerialTester) Close() error {
	return t.socket.Close()
}

func commandForTest(test *testsharder.Test, useRuntests bool, remoteOutputDir string, timeout time.Duration) ([]string, error) {
	command := []string{}
	if useRuntests {
		command = []string{runtestsName}
		if remoteOutputDir != "" {
			command = append(command, "--output", remoteOutputDir)
		}
		if timeout > 0 {
			command = append(command, "-i", fmt.Sprintf("%d", int64(timeout.Seconds())))
		}
		if test.PackageURL != "" {
			command = append(command, test.PackageURL)
		} else {
			command = append(command, test.Path)
		}
	} else if test.PackageURL != "" {
		if strings.HasSuffix(test.PackageURL, componentV2Suffix) {
			command = []string{runTestSuiteName}
			if test.Parallel != 0 {
				command = append(command, "--parallel", fmt.Sprintf("%d", test.Parallel))
			}
			// TODO(fxbug.dev/49262): Once fixed, combine timeout flag setting for v1 and v2.
			if timeout > 0 {
				command = append(command, "--timeout", fmt.Sprintf("%d", int64(timeout.Seconds())))
			}
		} else {
			command = []string{runTestComponentName}
			if test.LogSettings.MaxSeverity != "" {
				command = append(command, fmt.Sprintf("--max-log-severity=%s", test.LogSettings.MaxSeverity))
			}

			if timeout > 0 {
				command = append(command, fmt.Sprintf("--timeout=%d", int64(timeout.Seconds())))
			}
		}
		command = append(command, test.PackageURL)
	} else {
		return nil, fmt.Errorf("PackageURL is not set and useRuntests is false for %q", test.Name)
	}
	return command, nil
}
