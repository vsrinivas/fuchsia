// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testrunner

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

type fakeSSHClient struct {
	reconnectErrs  []error
	reconnectCalls int
	runErrs        []error
	runCalls       int
	lastCmd        []string
}

func (c *fakeSSHClient) Run(_ context.Context, command []string, _, _ io.Writer) error {
	c.runCalls++
	c.lastCmd = command
	if c.runErrs == nil {
		return nil
	}
	err, remainingErrs := c.runErrs[0], c.runErrs[1:]
	c.runErrs = remainingErrs
	return err
}

func (c *fakeSSHClient) Close() {}

func (c *fakeSSHClient) Reconnect(_ context.Context) error {
	c.reconnectCalls++
	if c.reconnectErrs == nil {
		return nil
	}
	err, remainingErrs := c.reconnectErrs[0], c.reconnectErrs[1:]
	c.reconnectErrs = remainingErrs
	return err
}

type fakeSerialClient struct {
	runCalls int
}

func (c *fakeSerialClient) runDiagnostics(_ context.Context) error {
	c.runCalls++
	return nil
}

type fakeCmdRunner struct {
	runErrs  []error
	runCalls int
	lastCmd  []string
}

func (r *fakeCmdRunner) Run(_ context.Context, command []string, _ subprocess.RunOptions) error {
	r.runCalls++
	r.lastCmd = command
	if r.runErrs == nil {
		return nil
	}
	err, remainingErrs := r.runErrs[0], r.runErrs[1:]
	r.runErrs = remainingErrs
	return err
}

func TestSubprocessTester(t *testing.T) {
	tmpDir := t.TempDir()
	passingTest := filepath.Join("host_x64", "passing")
	passingProfile := filepath.Join("llvm-profile", passingTest, "default.profraw")
	failingTest := filepath.Join("host_x64", "failing")
	failingProfile := filepath.Join("llvm-profile", failingTest, "default.profraw")
	for _, profile := range []string{passingProfile, failingProfile} {
		abs := filepath.Join(tmpDir, profile)
		os.MkdirAll(filepath.Dir(abs), 0o700)
		f, err := os.Create(abs)
		if err != nil {
			t.Fatalf("failed to create profile: %s", err)
		}
		f.Close()
	}
	// Override tempdir creation function to ensure deterministic
	// command output.
	newTempDir = func(dir, pattern string) (string, error) {
		return "/newtmp", nil
	}

	cases := []struct {
		name           string
		test           build.Test
		runErrs        []error
		expectedResult runtests.TestResult
		useSandboxing  bool
		env            map[string]string
		wantCmd        []string
		wantDataSinks  runtests.DataSinkMap
	}{
		{
			name:           "no path",
			test:           build.Test{},
			expectedResult: runtests.TestFailure,
		},
		{
			name:           "test passes with profile",
			test:           build.Test{Path: passingTest},
			expectedResult: runtests.TestSuccess,
			wantCmd:        []string{passingTest},
			wantDataSinks: runtests.DataSinkMap{
				"llvm-profile": []runtests.DataSink{
					{
						Name: filepath.Base(passingProfile),
						File: passingProfile,
					},
				},
			},
		},
		{
			name:          "test passes with profile and host test sandboxing",
			test:          build.Test{Path: passingTest},
			useSandboxing: true,
			env: map[string]string{
				llvmProfileEnvKey: "fake/llvm/profile/path/p.profraw",
			},
			expectedResult: runtests.TestSuccess,
			wantCmd: []string{
				"./fake_nsjail",
				"--disable_clone_newcgroup",
				"--disable_clone_newnet",
				"--bindmount_ro",
				"/bin:/bin",
				"--bindmount_ro",
				"/dev/kvm:/dev/kvm",
				"--bindmount_ro",
				"/dev/net/tun:/dev/net/tun",
				"--bindmount",
				"/dev/null:/dev/null",
				"--bindmount_ro",
				"/dev/urandom:/dev/urandom",
				"--bindmount_ro",
				"/dev/zero:/dev/zero",
				"--bindmount_ro",
				"/etc/alternatives/awk:/etc/alternatives/awk",
				"--bindmount_ro",
				"/etc/host.conf:/etc/host.conf",
				"--bindmount_ro",
				"/etc/hosts:/etc/hosts",
				"--bindmount_ro",
				"/etc/nsswitch.conf:/etc/nsswitch.conf",
				"--bindmount_ro",
				"/etc/passwd:/etc/passwd",
				"--bindmount_ro",
				"/etc/resolv.conf:/etc/resolv.conf",
				"--bindmount_ro",
				"/etc/ssl/certs:/etc/ssl/certs",
				"--bindmount_ro",
				"/lib:/lib",
				"--bindmount_ro",
				"/lib64:/lib64",
				"--bindmount",
				"/newtmp:/tmp",
				"--bindmount",
				fmt.Sprintf("%s:%s", tmpDir, tmpDir),
				"--bindmount",
				fmt.Sprintf("%s/host_x64/passing:%s/host_x64/passing", tmpDir, tmpDir),
				"--bindmount_ro",
				"/usr/bin:/usr/bin",
				"--bindmount_ro",
				"/usr/lib:/usr/lib",
				"--bindmount_ro",
				"/usr/share/misc/magic.mgc:/usr/share/misc/magic.mgc",
				"--bindmount_ro",
				"/usr/share/tcltk:/usr/share/tcltk",
				"--bindmount_ro",
				"/usr/share/vulkan:/usr/share/vulkan",
				"--symlink",
				"/proc/self/fd:/dev/fd",
				"--rlimit_as",
				"soft",
				"--rlimit_fsize",
				"soft",
				"--rlimit_nofile",
				"soft",
				"--rlimit_nproc",
				"soft",
				"--env",
				"ANDROID_TMP=/tmp",
				"--env",
				fmt.Sprintf("FUCHSIA_TEST_OUTDIR=%s/host_x64/passing", tmpDir),
				"--env",
				"HOME=/tmp",
				"--env",
				fmt.Sprintf("LLVM_PROFILE_FILE=%s/llvm-profile/host_x64/passing/%%m.profraw", tmpDir),
				"--env",
				"TEMP=/tmp",
				"--env",
				"TEMPDIR=/tmp",
				"--env",
				"TMP=/tmp",
				"--env",
				"TMPDIR=/tmp",
				"--env",
				"XDG_CACHE_HOME=/tmp",
				"--env",
				"XDG_CONFIG_HOME=/tmp",
				"--env",
				"XDG_DATA_HOME=/tmp",
				"--env",
				"XDG_HOME=/tmp",
				"--env",
				"XDG_STATE_HOME=/tmp",
				"--",
				passingTest,
			},
			wantDataSinks: runtests.DataSinkMap{
				"llvm-profile": []runtests.DataSink{
					{
						Name: filepath.Base(passingProfile),
						File: passingProfile,
					},
				},
			},
		},
		{
			name:           "test passes without profile",
			test:           build.Test{Path: "uninstrumented_test"},
			expectedResult: runtests.TestSuccess,
			wantCmd:        []string{"uninstrumented_test"},
			wantDataSinks:  nil,
		},
		{
			name:           "test fails",
			test:           build.Test{Path: failingTest},
			runErrs:        []error{fmt.Errorf("test failed")},
			expectedResult: runtests.TestFailure,
			wantCmd:        []string{failingTest},
			wantDataSinks: runtests.DataSinkMap{
				"llvm-profile": []runtests.DataSink{
					{
						Name: filepath.Base(failingProfile),
						File: failingProfile,
					},
				},
			},
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			runner := &fakeCmdRunner{
				runErrs: c.runErrs,
			}
			tester := SubprocessTester{
				localOutputDir: tmpDir,
			}
			if c.useSandboxing {
				tester.sProps = &sandboxingProps{
					nsjailPath: "./fake_nsjail",
				}
				// Override the environment to make sure we have a deterministic
				// command.
				prevEnv := os.Environ()
				prev := make(map[string]string)
				for _, entry := range prevEnv {
					parts := strings.SplitN(entry, "=", 2)
					os.Unsetenv(parts[0])
					prev[parts[0]] = parts[1]
				}
				for k, v := range c.env {
					os.Setenv(k, v)
				}
				// Restore the environment after the test case.
				t.Cleanup(func() {
					for k := range c.env {
						os.Unsetenv(k)
					}
					for k, v := range prev {
						os.Setenv(k, v)
					}
				})
			}
			newRunner = func(dir string, env []string) cmdRunner {
				return runner
			}
			outDir := filepath.Join(tmpDir, c.test.Path)
			testResult, err := tester.Test(context.Background(), testsharder.Test{Test: c.test}, io.Discard, io.Discard, outDir)
			if err != nil {
				t.Errorf("tester.Test got error: %s, want nil", err)
			}
			if c.expectedResult != testResult.Result {
				t.Errorf("tester.Test got result: %s, want: %s", testResult.Result, c.expectedResult)
			}
			if _, statErr := os.Stat(outDir); statErr != nil {
				t.Error("tester.Test did not create a readable outDir:", statErr)
			}
			var opts []cmp.Option
			// We sort the cmd when using sandboxing because the mounts are
			// ordered alphabetically.
			if c.useSandboxing {
				opts = append(opts, cmpopts.SortSlices(func(s, t string) bool {
					return s < t
				}))
			}
			if diff := cmp.Diff(c.wantCmd, runner.lastCmd, opts...); diff != "" {
				t.Errorf("Unexpected command run (-want +got):\n%s", diff)
			}

			sinks := testResult.DataSinks.Sinks
			if diff := cmp.Diff(c.wantDataSinks, sinks); diff != "" {
				t.Errorf("Diff in data sinks (-want +got):\n%s", diff)
			}
		})
	}
}

type fakeDataSinkCopier struct {
	reconnectCalls int
	remoteDirs     map[string]struct{}
}

func (c *fakeDataSinkCopier) GetAllDataSinks(remoteDir string) ([]runtests.DataSink, error) {
	c.remoteDirs[remoteDir] = struct{}{}
	return []runtests.DataSink{{Name: "sink", File: "sink"}}, nil
}

func (c *fakeDataSinkCopier) GetReferences(remoteDir string) (map[string]runtests.DataSinkReference, error) {
	c.remoteDirs[remoteDir] = struct{}{}
	return map[string]runtests.DataSinkReference{}, nil
}

func (*fakeDataSinkCopier) Copy(_ []runtests.DataSinkReference, _ string) (runtests.DataSinkMap, error) {
	return runtests.DataSinkMap{}, nil
}

func (c *fakeDataSinkCopier) Reconnect() error {
	c.reconnectCalls++
	return nil
}

func (*fakeDataSinkCopier) Close() error {
	return nil
}

func TestFFXTester(t *testing.T) {
	cases := []struct {
		name            string
		runV2           bool
		sshRunErrs      []error
		expectedResult  runtests.TestResult
		testMulti       bool
		experimentLevel int
	}{
		{
			name:           "run v1 tests with ssh",
			sshRunErrs:     []error{nil},
			expectedResult: runtests.TestSuccess,
		},
		{
			name:           "run v2 tests with ffx",
			runV2:          true,
			expectedResult: runtests.TestSuccess,
		},
		{
			name:           "ffx test fails",
			runV2:          true,
			expectedResult: runtests.TestFailure,
		},
		{
			name:           "ffx test times out",
			runV2:          true,
			expectedResult: runtests.TestAborted,
		},
		{
			name:           "ffx test skipped",
			runV2:          true,
			expectedResult: runtests.TestSkipped,
		},
		{
			name:            "run multiple tests",
			runV2:           true,
			expectedResult:  runtests.TestSuccess,
			testMulti:       true,
			experimentLevel: 3,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			client := &fakeSSHClient{
				runErrs: c.sshRunErrs,
			}
			copier := &fakeDataSinkCopier{remoteDirs: make(map[string]struct{})}
			sshTester := &FuchsiaSSHTester{
				client:                      client,
				copier:                      copier,
				connectionErrorRetryBackoff: &retry.ZeroBackoff{},
				useRuntests:                 true,
			}
			var outcome string
			switch c.expectedResult {
			case runtests.TestSuccess:
				outcome = ffxutil.TestPassed
			case runtests.TestFailure:
				outcome = ffxutil.TestFailed
			case runtests.TestAborted:
				outcome = ffxutil.TestTimedOut
			case runtests.TestSkipped:
				outcome = ffxutil.TestNotStarted
			}
			ffx := &ffxutil.MockFFXInstance{TestOutcome: outcome}
			tester := NewFFXTester(ffx, sshTester, t.TempDir(), c.experimentLevel)

			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %s", err)
				}
			}()

			test := testsharder.Test{
				Test:         build.Test{PackageURL: "fuchsia-pkg://foo"},
				Runs:         1,
				RunAlgorithm: testsharder.StopOnSuccess,
			}
			if c.runV2 || c.testMulti {
				test.Test = build.Test{PackageURL: "fuchsia-pkg://foo#meta/bar.cm"}
			}
			ctx := context.Background()
			var testResults []*TestResult
			var err error
			if c.testMulti {
				tests := []testsharder.Test{
					test,
					{
						Test:         build.Test{PackageURL: "fuchsia-pkg://foo#meta/baz.cm"},
						Runs:         1,
						RunAlgorithm: testsharder.StopOnSuccess,
					},
				}
				testResults, err = tester.TestMultiple(ctx, tests, io.Discard, io.Discard, t.TempDir())
			} else {
				var testResult *TestResult
				testResult, err = tester.Test(ctx, test, io.Discard, io.Discard, t.TempDir())
				testResults = []*TestResult{testResult}
			}
			if err != nil {
				t.Errorf("tester.Test got unexpected error: %s", err)
			}
			var sinks []runtests.DataSinkReference
			for _, testResult := range testResults {
				if testResult.Result != c.expectedResult {
					t.Errorf("tester.Test got result: %s, want result: %s", testResult.Result, c.expectedResult)
				}

				if c.runV2 {
					testArgs := []string{}
					if c.experimentLevel == 3 {
						testArgs = append(testArgs, "--experimental-parallel-execution", "8")
					}
					if !ffx.ContainsCmd("test", testArgs...) {
						t.Errorf("failed to call `ffx test`, called: %s", ffx.CmdsCalled)
					}
					expectedCaseStatus := runtests.TestSuccess
					if c.expectedResult != runtests.TestSuccess {
						expectedCaseStatus = runtests.TestFailure
					}
					if len(testResult.Cases) != 1 {
						t.Errorf("expected 1 test case, got %d", len(testResult.Cases))
					} else {
						if testResult.Cases[0].Status != expectedCaseStatus {
							t.Errorf("test case has status: %s, want: %s", testResult.Cases[0].Status, expectedCaseStatus)
						}
					}
				} else {
					if ffx.ContainsCmd("test") {
						t.Errorf("unexpectedly called ffx test")
					}
				}
				sinks = append(sinks, testResult.DataSinks)
			}
			p := filepath.Join(t.TempDir(), "testrunner-cmd-test")
			if err = tester.RunSnapshot(ctx, p); err != nil {
				t.Errorf("failed to run snapshot: %s", err)
			}
			if !ffx.ContainsCmd("snapshot") {
				t.Errorf("failed to call `ffx target snapshot`, called: %s", ffx.CmdsCalled)
			}

			wantSSHRunCalls := len(c.sshRunErrs)
			if wantSSHRunCalls != client.runCalls {
				t.Errorf("Run() called wrong number of times. Got: %d, Want: %d", client.runCalls, wantSSHRunCalls)
			}

			if c.runV2 {
				// Call EnsureSinks() for v2 tests to set the copier.remoteDir to the data output dir for v2 tests.
				// v1 tests will already have set the appropriate remoteDir value within Test().
				outputs := &TestOutputs{OutDir: t.TempDir()}
				if err = tester.EnsureSinks(ctx, sinks, outputs); err != nil {
					t.Errorf("failed to collect sinks: %s", err)
				}
				foundEarlyBootSinks := false
				for _, test := range outputs.Summary.Tests {
					if test.Name == "early_boot_sinks" {
						foundEarlyBootSinks = true
						if len(test.DataSinks["llvm-profile"]) != 1 {
							t.Errorf("got %d early boot sinks, want 1", len(test.DataSinks["llvm-profile"]))
						}
						break
					}
				}
				if !foundEarlyBootSinks {
					t.Errorf("failed to find early boot sinks")
				}
			} else {
				if _, ok := copier.remoteDirs[dataOutputDir]; !ok {
					t.Errorf("expected sinks in dir: %s, but got: %s", dataOutputDir, copier.remoteDirs)
				}
			}
		})
	}
}

// for testability
type fakeSSHExitError struct {
	exitStatus int
}

// Statically assert that this error satisfies the sshExitError interface.
var _ sshExitError = fakeSSHExitError{}

func (e fakeSSHExitError) Error() string   { return fmt.Sprintf("ssh exit status: %d", e.exitStatus) }
func (e fakeSSHExitError) ExitStatus() int { return e.exitStatus }

func TestSSHTester(t *testing.T) {
	cases := []struct {
		name            string
		runErrs         []error
		reconErrs       []error
		copierReconErrs []error
		expectedResult  runtests.TestResult
		wantConnErr     bool
		runSnapshot     bool
		useRuntests     bool
		runV2           bool
	}{
		{
			name:    "success",
			runErrs: []error{nil},
		},
		{
			name:           "test failure",
			runErrs:        []error{fakeSSHExitError{exitStatus: 1}},
			expectedResult: runtests.TestFailure,
		},
		{
			name:           "connection error retry and test failure",
			runErrs:        []error{sshutil.ConnectionError{}, fmt.Errorf("test failed")},
			reconErrs:      []error{nil},
			expectedResult: runtests.TestFailure,
		},
		{
			name:      "reconnect succeeds then fails",
			runErrs:   []error{sshutil.ConnectionError{}, sshutil.ConnectionError{}},
			reconErrs: []error{nil, fmt.Errorf("reconnect failed")},
			// Make sure we return the original ConnectionError and not the error from the failed
			// reconnect attempt. This is important because the code that calls Test() in a loop
			// aborts the loop when it sees an ConnectionError.
			wantConnErr: true,
		},
		{
			name:           "reconnect succeeds thrice",
			runErrs:        []error{sshutil.ConnectionError{}, sshutil.ConnectionError{}, sshutil.ConnectionError{}},
			reconErrs:      []error{nil, nil, nil},
			expectedResult: runtests.TestFailure,
			// Reconnection succeeds so we don't want the caller to see a ConnectionError.
			wantConnErr: false,
		},
		{
			name:        "reconnect before snapshot",
			runErrs:     []error{nil, sshutil.ConnectionError{}, nil},
			reconErrs:   []error{nil},
			runSnapshot: true,
		},
		{
			name:        "get data sinks for v1 tests",
			runErrs:     []error{nil},
			useRuntests: true,
		},
		{
			name:        "get data sinks for v2 tests",
			runErrs:     []error{nil},
			useRuntests: true,
			runV2:       true,
		},
		{
			name:           "test timeout",
			runErrs:        []error{fakeSSHExitError{exitStatus: timeoutExitCode}},
			expectedResult: runtests.TestAborted,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			client := &fakeSSHClient{
				reconnectErrs: c.reconErrs,
				runErrs:       c.runErrs,
			}
			copier := &fakeDataSinkCopier{remoteDirs: make(map[string]struct{})}
			serialSocket := &fakeSerialClient{}
			var tester Tester
			tester = &FuchsiaSSHTester{
				client:                      client,
				copier:                      copier,
				connectionErrorRetryBackoff: &retry.ZeroBackoff{},
				serialSocket:                serialSocket,
				useRuntests:                 c.useRuntests,
			}

			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %s", err)
				}
			}()
			wantReconnCalls := len(c.reconErrs)
			wantRunCalls := len(c.runErrs)
			test := testsharder.Test{
				Test:         build.Test{PackageURL: "fuchsia-pkg://foo"},
				Runs:         1,
				RunAlgorithm: testsharder.StopOnSuccess,
				Timeout:      time.Second,
			}
			if c.runV2 {
				test.Test = build.Test{PackageURL: "fuchsia-pkg://foo#meta/bar.cm"}
			}
			testResult, err := tester.Test(context.Background(), test, io.Discard, io.Discard, "unused-out-dir")
			expectedResult := runtests.TestSuccess
			if c.expectedResult != "" {
				expectedResult = c.expectedResult
			}
			if err == nil && c.wantConnErr {
				t.Errorf("tester.Test got nil error, want non-nil error")
			} else if err != nil {
				if !c.wantConnErr {
					t.Errorf("tester.Test got error: %s, want nil", err)
				} else if !sshutil.IsConnectionError(err) {
					t.Errorf("tester.Test got error: %s. want conn err", err)
				}
			}
			if !c.wantConnErr && testResult.Result != expectedResult {
				t.Errorf("got test result: %s, want: %s", testResult.Result, expectedResult)
			}

			if c.runSnapshot {
				p := filepath.Join(t.TempDir(), "testrunner-cmd-test")
				if err = tester.RunSnapshot(context.Background(), p); err != nil {
					t.Errorf("failed to run snapshot: %s", err)
				}
			}
			if c.runV2 {
				outputs := &TestOutputs{OutDir: t.TempDir()}
				if err = tester.EnsureSinks(context.Background(), []runtests.DataSinkReference{testResult.DataSinks}, outputs); err != nil {
					t.Errorf("failed to collect v2 sinks: %s", err)
				}
				foundEarlyBootSinks := false
				for _, test := range outputs.Summary.Tests {
					if test.Name == "early_boot_sinks" {
						foundEarlyBootSinks = true
						if len(test.DataSinks["llvm-profile"]) != 1 {
							t.Errorf("got %d early boot sinks, want 1", len(test.DataSinks["llvm-profile"]))
						}
						break
					}
				}
				if !foundEarlyBootSinks {
					t.Errorf("failed to find early boot sinks")
				}
			}

			if wantReconnCalls != client.reconnectCalls {
				t.Errorf("Reconnect() called wrong number of times. Got: %d, Want: %d", client.reconnectCalls, wantReconnCalls)
			}

			reconnFailures := 0
			for _, err := range c.reconErrs {
				if err != nil {
					reconnFailures++
				}
			}
			// The copier shouldn't be reconnected if reconnecting the ssh
			// client fails.
			wantCopierReconnCalls := wantReconnCalls - reconnFailures
			if wantCopierReconnCalls != copier.reconnectCalls {
				t.Errorf("Reconnect() called wrong number of times. Got: %d, Want: %d", copier.reconnectCalls, wantReconnCalls)
			}

			if wantRunCalls != client.runCalls {
				t.Errorf("Run() called wrong number of times. Got: %d, Want: %d", client.runCalls, wantRunCalls)
			}

			if c.wantConnErr {
				if serialSocket.runCalls != 1 {
					t.Errorf("called RunSerialDiagnostics() %d times", serialSocket.runCalls)
				}
			} else if serialSocket.runCalls > 0 {
				t.Errorf("unexpectedly called RunSerialDiagnostics()")
			}

			if c.useRuntests {
				expectedRemoteDirs := []string{dataOutputDir}
				if c.runV2 {
					expectedRemoteDirs = []string{dataOutputDirV2, dataOutputDirEarlyBoot}
				}
				for _, dir := range expectedRemoteDirs {
					if _, ok := copier.remoteDirs[dir]; !ok {
						t.Errorf("expected sinks in dir: %s, but got: %s", dir, copier.remoteDirs)
					}
				}
			}
		})
	}
}

// Creates pair of ReadWriteClosers that mimics the relationship between serial
// and socket i/o. Implemented with in-memory pipes, the input of one can
// synchronously by read as the output of the other.
func serialAndSocket() (socketConn, socketConn) {
	rSerial, wSocket := io.Pipe()
	rSocket, wSerial := io.Pipe()
	serial := &joinedPipeEnds{rSerial, wSerial}
	socket := &joinedPipeEnds{rSocket, wSocket}
	return serial, socket
}

type fakeSerialServer struct {
	received       []byte
	shutdownString string
	socketPath     string
	listeningChan  chan bool
}

func (s *fakeSerialServer) Serve() error {
	listener, err := net.Listen("unix", s.socketPath)
	if err != nil {
		s.listeningChan <- false
		return fmt.Errorf("Listen(%s) failed: %w", s.socketPath, err)
	}
	defer listener.Close()
	s.listeningChan <- true
	conn, err := listener.Accept()
	if err != nil {
		return fmt.Errorf("Accept() failed: %w", err)
	}
	defer conn.Close()
	// Signal we're ready to accept input.
	if _, err := conn.Write([]byte(serialConsoleCursor)); err != nil {
		return fmt.Errorf("conn.Write() failed: %w", err)
	}
	reader := iomisc.NewMatchingReader(conn, []byte(s.shutdownString))
	for {
		buf := make([]byte, 1024)
		bytesRead, err := reader.Read(buf)
		s.received = append(s.received, buf[:bytesRead]...)
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return fmt.Errorf("conn.Read() failed: %w", err)
		}
	}
}

// fakeContext conforms to context.Context but lets us control the return
// value of Err().
type fakeContext struct {
	sync.Mutex
	err error
}

func (ctx *fakeContext) Deadline() (time.Time, bool) {
	return time.Time{}, false
}

func (ctx *fakeContext) Done() <-chan struct{} {
	ch := make(chan struct{})
	close(ch)
	return ch
}

func (ctx *fakeContext) Err() error {
	ctx.Lock()
	defer ctx.Unlock()
	return ctx.err
}

func (ctx *fakeContext) SetErr(err error) {
	ctx.Lock()
	ctx.err = err
	ctx.Unlock()
}

func (ctx *fakeContext) Value(key interface{}) interface{} {
	return nil
}

func TestSerialTester(t *testing.T) {
	cases := []struct {
		name           string
		expectedResult runtests.TestResult
		wantErr        bool
		wantRetry      bool
	}{
		{
			name:           "test passes",
			expectedResult: runtests.TestSuccess,
		}, {
			name:           "test fails",
			expectedResult: runtests.TestFailure,
		}, {
			name:           "test does not start on first try",
			expectedResult: runtests.TestSuccess,
			wantRetry:      true,
		}, {
			name:    "test returns fatal err",
			wantErr: true,
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			ctx := context.Background()
			serial, socket := serialAndSocket()
			defer socket.Close()
			defer serial.Close()

			tester := FuchsiaSerialTester{socket: socket}
			test := testsharder.Test{
				Test: build.Test{
					Name: "myfoo",
					Path: "foo",
				},
			}
			expectedCmd := "\r\nruntests foo\r\n"

			if tc.wantRetry {
				oldNewTestStartedContext := newTestStartedContext
				defer func() {
					newTestStartedContext = oldNewTestStartedContext
				}()
			}
			var fakeTestStartedContext fakeContext
			fakeTestStartedCancel := func() {}
			newTestStartedContext = func(ctx context.Context) (context.Context, context.CancelFunc) {
				return &fakeTestStartedContext, fakeTestStartedCancel
			}

			type testResult struct {
				result *TestResult
				err    error
			}
			results := make(chan testResult)
			var stdout bytes.Buffer
			go func() {
				result, err := tester.Test(ctx, test, &stdout, io.Discard, "unused-out-dir")
				results <- testResult{result, err}
			}()

			expectedTries := 1
			if tc.wantRetry {
				// Ensure tester times out waiting for the test to start.
				fakeTestStartedContext.SetErr(context.DeadlineExceeded)
				expectedTries = 2
			}

			// The write to the socket will block until we read from serial.
			buff := make([]byte, len(expectedCmd))
			for i := 0; i < expectedTries; i++ {
				if _, err := io.ReadFull(serial, buff); err != nil {
					t.Errorf("error reading from serial: %s", err)
				} else if string(buff) != expectedCmd {
					t.Errorf("unexpected command: %s", buff)
				}
			}

			if tc.wantRetry {
				fakeTestStartedContext.SetErr(nil)

				// At this point, the tester is either waiting for the startedSignature
				// (we set the startedContext error to nil before it started waiting for
				// it) or it's waiting to write to the socket again for its next retry.
				// In the latter case, we should read the remaining bytes from serial to
				// unblock the tester's write to the socket.
				// Put in a goroutine to not block in the case that there's nothing to read.
				// This will return when the serial is closed at the end of the test.
				go func() {
					if b, err := io.ReadAll(serial); err != nil {
						fmt.Println(err)
						results <- testResult{nil, fmt.Errorf("error reading from serial: %w", err)}
					} else if string(b) != expectedCmd || string(b) != "" {
						results <- testResult{nil, fmt.Errorf("unexpected serial input: %s", string(buff))}
					}
				}()
			}

			// At this point, the tester will be blocked reading from the socket.
			started := runtests.StartedSignature + test.Name
			testStatus := runtests.SuccessSignature
			if tc.expectedResult == runtests.TestFailure {
				testStatus = runtests.FailureSignature
			}
			testReturn := testStatus + test.Name

			if tc.wantErr {
				// Close the serial connection so that the tester returns a fatal error.
				serial.Close()
			} else {
				if _, err := io.WriteString(serial, started); err != nil {
					t.Errorf("failed to write %s to serial", started)
				}
				if _, err := io.WriteString(serial, testReturn); err != nil {
					t.Errorf("failed to write %s to serial", testReturn)
				}
			}

			select {
			case r := <-results:
				if gotErr := r.err != nil; gotErr != tc.wantErr {
					t.Errorf("test got err: %s, want err: %t", r.err, tc.wantErr)
				}
				if !tc.wantErr && r.result == nil {
					t.Error("got nil test result")
				}
				if !tc.wantErr && tc.expectedResult != r.result.Result {
					t.Errorf("test got result: %s, want: %s", r.result.Result, tc.expectedResult)
				}
			}
			stdoutBytes := stdout.Bytes()
			if !tc.wantErr && !bytes.Contains(stdoutBytes, []byte(started+testReturn)) {
				t.Errorf("Expected stdout to contain %q, got %q", started+testReturn, string(stdoutBytes))
			}
		})
	}
}

func longKernelLog(numChars int) string {
	kernelLog := "[123.456]"
	for i := 0; i < numChars; i++ {
		kernelLog += "a"
	}
	return kernelLog + "\n"
}

func longPotentialKernelLog(numChars int) string {
	log := "["
	for i := 0; i < numChars; i++ {
		log += "1"
	}
	return log
}

type hangForeverReader struct {
	readCalled chan struct{}
	readChan   chan struct{}
}

func (r *hangForeverReader) Read(buf []byte) (int, error) {
	r.readCalled <- struct{}{}
	<-r.readChan
	return 0, nil
}

func TestParseOutKernelReader(t *testing.T) {
	cases := []struct {
		name           string
		output         string
		expectedOutput string
	}{
		{
			name:           "no kernel logs",
			output:         "line1\n[line2]output\nline3[output]\nline4[out",
			expectedOutput: "line1\n[line2]output\nline3[output]\nline4[out",
		}, {
			name:           "with kernel logs",
			output:         "line1\n[123.456]kernel line1\noutput[123.456]kernel line2\noutput continued [bracket] output [123.456] kernel [line3]\noutput continued[123]\n",
			expectedOutput: "line1\noutputoutput continued [bracket] output output continued",
		}, {
			name:           "long kernel log",
			output:         "line1" + longKernelLog(2000) + "output continued\n",
			expectedOutput: "line1output continued\n",
		}, {
			name: "long potential kernel log",
			// This should test the proper reading of logs which need to store both
			// kernel and non-kernel logs for later processing.
			output:         "line1" + longPotentialKernelLog(2000) + "\noutput continued[123",
			expectedOutput: "line1" + longPotentialKernelLog(2000) + "\noutput continued[123",
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			r := &parseOutKernelReader{
				ctx:    context.Background(),
				reader: strings.NewReader(c.output),
			}
			b, err := io.ReadAll(r)
			if err != nil {
				t.Fatal(err)
			}
			if string(b) != c.expectedOutput {
				t.Errorf("expected: %s, got: %s", c.expectedOutput, string(b))
			}
		})
	}
	t.Run("canceled context", func(t *testing.T) {
		ctx, cancel := context.WithCancel(context.Background())
		reader := &hangForeverReader{readCalled: make(chan struct{}), readChan: make(chan struct{})}
		r := &parseOutKernelReader{
			ctx:    ctx,
			reader: reader,
		}
		errs := make(chan error)
		go func() {
			_, err := io.ReadAll(r)
			errs <- err
		}()
		// Wait for Read() to be called before canceling the context.
		<-reader.readCalled
		cancel()
		if err := <-errs; !errors.Is(err, ctx.Err()) {
			t.Errorf("expected: %s, got: %s", ctx.Err(), err)
		}
		close(reader.readChan)
	})
}

func TestCommandForTest(t *testing.T) {
	cases := []struct {
		name        string
		test        testsharder.Test
		useRuntests bool
		timeout     time.Duration
		expected    []string
		wantErr     bool
	}{
		{
			name:        "use runtests URL",
			useRuntests: true,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				},
			},
			expected: []string{"runtests", "--output", "REMOTE_DIR", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "use runtests path",
			useRuntests: true,
			test: testsharder.Test{
				Test: build.Test{
					Path: "/path/to/test",
				},
			},
			expected: []string{"runtests", "--output", "REMOTE_DIR", "/path/to/test"},
		},
		{
			name:        "use runtests timeout",
			useRuntests: true,
			test: testsharder.Test{
				Test: build.Test{
					Path: "/path/to/test",
				},
			},
			timeout:  time.Second,
			expected: []string{"runtests", "--output", "REMOTE_DIR", "-i", "1", "/path/to/test"},
		},
		{
			name:        "use runtests realm-label",
			useRuntests: true,
			test: testsharder.Test{
				Test: build.Test{
					Path: "/path/to/test",
				},
				RealmLabel: "testrealm",
			},
			expected: []string{"runtests", "--output", "REMOTE_DIR", "--realm-label", "testrealm", "/path/to/test"},
		},
		{
			name:        "system path",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path: "/path/to/test",
				},
			},
			wantErr: true,
		},
		{
			name:        "components v1",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				},
			},
			expected: []string{"run-test-component", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v1 timeout",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				},
			},
			timeout:  time.Second,
			expected: []string{"run-test-component", "--timeout=1", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v1 max severity",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:        "/path/to/test",
					PackageURL:  "fuchsia-pkg://example.com/test.cmx",
					LogSettings: build.LogSettings{MaxSeverity: "ERROR"},
				},
			},
			timeout:  time.Second,
			expected: []string{"run-test-component", "--max-log-severity=ERROR", "--timeout=1", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v1 realm label",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				},
				RealmLabel: "testrealm",
			},
			expected: []string{"run-test-component", "--realm-label", "testrealm", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v2",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
				},
			},
			expected: []string{"run-test-suite", "--filter-ansi", "fuchsia-pkg://example.com/test.cm"},
		},
		{
			name:        "components v2 no parallel",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
				},
			},
			expected: []string{"run-test-suite", "--filter-ansi", "fuchsia-pkg://example.com/test.cm"},
		},
		{
			name:        "components v2 parallel",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
					Parallel:   2,
				},
			},
			expected: []string{"run-test-suite", "--filter-ansi", "--parallel", "2", "fuchsia-pkg://example.com/test.cm"},
		},
		{
			name:        "components v2 timeout",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
				},
			},
			timeout:  time.Second,
			expected: []string{"run-test-suite", "--filter-ansi", "--timeout", "1", "fuchsia-pkg://example.com/test.cm"},
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			command, err := commandForTest(&c.test, c.useRuntests, "REMOTE_DIR", c.timeout)
			if err == nil {
				if c.wantErr {
					t.Errorf("commandForTest returned nil error, want non-nil error")
				}
			} else if !c.wantErr {
				t.Errorf("commandForTest returned error: %s, want nil", err)
			}
			if diff := cmp.Diff(c.expected, command); diff != "" {
				t.Errorf("unexpected command (-want +got):\n%s", diff)
			}
		})
	}
}

type joinedPipeEnds struct {
	r *io.PipeReader
	w *io.PipeWriter
}

func (pe *joinedPipeEnds) Read(p []byte) (int, error) {
	return pe.r.Read(p)
}

func (pe *joinedPipeEnds) Write(p []byte) (int, error) {
	return pe.w.Write(p)
}

func (pe *joinedPipeEnds) SetIOTimeout(_ time.Duration) {
}

func (pe *joinedPipeEnds) Close() error {
	if err := pe.r.Close(); err != nil {
		pe.w.Close()
		return err
	}
	return pe.w.Close()
}
