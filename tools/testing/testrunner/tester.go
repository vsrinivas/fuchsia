// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package testrunner

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"math"
	"net"
	"os"
	"os/user"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"

	botanistconstants "go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/clock"
	"go.fuchsia.dev/fuchsia/tools/lib/environment"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/serial"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
)

const (
	// A test output directory within persistent storage.
	dataOutputDir = "/data/infra/testrunner"

	// TODO(fxb/73171): Fix this path.
	// The output data directory for component v2 tests.
	dataOutputDirV2 = "/tmp/test_manager:0/data/debug"

	// The output data directory for early boot coverage.
	dataOutputDirEarlyBoot = "/tmp/test_manager:0/data/kernel_debug"

	// Various tools for running tests.
	runtestsName         = "runtests"
	runTestComponentName = "run-test-component"
	runTestSuiteName     = "run-test-suite"

	// Returned by both run-test-component and run-test-suite to indicate the
	// test timed out.
	timeoutExitCode = 21

	// Printed to the serial console when ready to accept user input.
	serialConsoleCursor = "\n$"

	// Number of times to try running a test command over serial before giving
	// up. This value was somewhat arbitrarily chosen and can be adjusted higher
	// or lower if deemed appropriate.
	startSerialCommandMaxAttempts = 3

	llvmProfileEnvKey    = "LLVM_PROFILE_FILE"
	llvmProfileExtension = ".profraw"
	llvmProfileSinkType  = "llvm-profile"

	testStartedTimeout = 5 * time.Second

	// The name of the test to associate early boot data sinks with.
	earlyBootSinksTestName = "early_boot_sinks"
)

// Tester describes the interface for all different types of testers.
type Tester interface {
	Test(context.Context, testsharder.Test, io.Writer, io.Writer, string) (*TestResult, error)
	Close() error
	EnsureSinks(context.Context, []runtests.DataSinkReference, *TestOutputs) error
	RunSnapshot(context.Context, string) error
}

// For testability
type cmdRunner interface {
	Run(ctx context.Context, command []string, stdout, stderr io.Writer) error
}

// For testability
var newRunner = func(dir string, env []string) cmdRunner {
	return &subprocess.Runner{Dir: dir, Env: env}
}

// For testability.
var newTempDir = func(dir, pattern string) (string, error) {
	return os.MkdirTemp(dir, pattern)
}

// For testability
type sshClient interface {
	Close()
	Reconnect(ctx context.Context) error
	Run(ctx context.Context, command []string, stdout, stderr io.Writer) error
}

// For testability
type dataSinkCopier interface {
	GetAllDataSinks(remoteDir string) ([]runtests.DataSink, error)
	GetReferences(remoteDir string) (map[string]runtests.DataSinkReference, error)
	Copy(sinks []runtests.DataSinkReference, localDir string) (runtests.DataSinkMap, error)
	Reconnect() error
	Close() error
}

// For testability
type serialClient interface {
	runDiagnostics(ctx context.Context) error
}

// BaseTestResultFromTest returns a TestResult for a Tester.Test() to modify
// and return with some pre-filled values and a starting failure result which
// should be changed as needed within the tester's Test() method.
func BaseTestResultFromTest(test testsharder.Test) *TestResult {
	return &TestResult{
		Name:      test.Name,
		GNLabel:   test.Label,
		Result:    runtests.TestFailure,
		DataSinks: runtests.DataSinkReference{},
		Tags:      test.Tags,
	}
}

// SubprocessTester executes tests in local subprocesses.
type SubprocessTester struct {
	env            []string
	dir            string
	localOutputDir string
	sProps         *sandboxingProps
}

type sandboxingProps struct {
	nsjailPath    string
	nsjailRoot    string
	mountQEMU     bool
	mountUserHome bool
	cwd           string
}

// NewSubprocessTester returns a SubprocessTester that can execute tests
// locally with a given working directory and environment.
func NewSubprocessTester(dir string, env []string, localOutputDir, nsjailPath, nsjailRoot string) (Tester, error) {
	s := &SubprocessTester{
		dir:            dir,
		env:            env,
		localOutputDir: localOutputDir,
	}
	// If the caller provided a path to NsJail, then intialize sandboxing properties.
	if nsjailPath != "" {
		s.sProps = &sandboxingProps{
			nsjailPath: nsjailPath,
			nsjailRoot: nsjailRoot,
			// TODO(rudymathu): Remove this once ssh/ssh-keygen usage is removed.
			mountUserHome: true,
		}

		if _, err := os.Stat("/sys/class/net/qemu/"); err == nil {
			s.sProps.mountQEMU = true
		} else if !errors.Is(err, os.ErrNotExist) {
			return &SubprocessTester{}, nil
		}

		cwd, err := os.Getwd()
		if err != nil {
			return &SubprocessTester{}, err
		}
		s.sProps.cwd = cwd
	}
	return s, nil
}

func (t *SubprocessTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer, outDir string) (*TestResult, error) {
	testResult := BaseTestResultFromTest(test)
	if test.Path == "" {
		testResult.FailReason = fmt.Sprintf("test %q has no `path` set", test.Name)
		return testResult, nil
	}
	// Some tests read TestOutDirEnvKey so ensure they get their own output dir.
	if err := os.MkdirAll(outDir, 0o770); err != nil {
		testResult.FailReason = err.Error()
		return testResult, nil
	}

	// Might as well emit any profiles directly to the output directory.
	// We'll set
	// LLVM_PROFILE_FILE=<output dir>/<test-specific namsepace>/%m.profraw
	// and then record any .profraw file written to that directory as an
	// emitted profile.
	profileRelDir := filepath.Join(llvmProfileSinkType, test.Path)
	profileAbsDir := filepath.Join(t.localOutputDir, profileRelDir)
	os.MkdirAll(profileAbsDir, os.ModePerm)

	r := newRunner(t.dir, append(
		t.env,
		fmt.Sprintf("%s=%s", constants.TestOutDirEnvKey, outDir),
		// When host-side tests are instrumented for profiling, executing
		// them will write a profile to the location under this environment variable.
		fmt.Sprintf("%s=%s", llvmProfileEnvKey, filepath.Join(profileAbsDir, "%m"+llvmProfileExtension)),
	))
	if test.Timeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, test.Timeout)
		defer cancel()
	}
	testCmd := []string{test.Path}
	if t.sProps != nil {
		testCmdBuilder := &NsJailCmdBuilder{
			Bin: t.sProps.nsjailPath,
			// TODO(rudymathu): Eventually, this should be a more fine grained
			// property that disables network isolation only on tests that explicitly
			// request it.
			IsolateNetwork: false,
			MountPoints: []*MountPt{
				{
					Src:      t.localOutputDir,
					Writable: true,
				},
				{
					Src:      outDir,
					Writable: true,
				},
				{
					// The fx_script_tests utilize this file.
					Src: "/usr/share/misc/magic.mgc",
				},
			},
			Symlinks: map[string]string{
				"/proc/self/fd": "/dev/fd",
			},
		}

		// Mount the QEMU tun_flags if the qemu interface exists. This is used
		// by VDL to ascertain that the interface exists.
		if t.sProps.mountQEMU {
			testCmdBuilder.MountPoints = append(
				testCmdBuilder.MountPoints,
				&MountPt{
					Src: "/sys/class/net/qemu/",
				},
			)
		}

		// Some tests invoke the `ssh` command line tool, which always creates
		// a .ssh file in the home directory. Unfortunately, it prefers to read
		// the home directory from the /etc/passwd file, and only reads $HOME
		// if this doesn't work. Because we need to mount /etc/passwd for
		// ssh-keygen, we need to create the same home directory in
		// /etc/passwd. This is really quite a big hack, and we should remove
		// it ASAP.
		if t.sProps.mountUserHome {
			currentUser, err := user.Current()
			if err != nil {
				testResult.FailReason = err.Error()
				return testResult, nil
			}
			pwdFile, err := os.Open("/etc/passwd")
			if err != nil {
				testResult.FailReason = err.Error()
				return testResult, nil
			}
			defer pwdFile.Close()
			pwdScanner := bufio.NewScanner(pwdFile)
			for pwdScanner.Scan() {
				elems := strings.Split(pwdScanner.Text(), ":")
				if elems[0] == currentUser.Username {
					testCmdBuilder.MountPoints = append(
						testCmdBuilder.MountPoints,
						&MountPt{
							Dst:      elems[5],
							UseTmpfs: true,
						},
					)
					break
				}
			}
			if pwdScanner.Err() != nil {
				testResult.FailReason = pwdScanner.Err().Error()
				return testResult, nil
			}
		}

		// Mount /tmp. Ideally, we would use a tmpfs mount, but we write quite a
		// lot of data to it, so we instead create a temp dir and mount it
		// instead.
		tmpDir, err := newTempDir("", "")
		if err != nil {
			testResult.FailReason = err.Error()
			return testResult, nil
		}
		defer os.RemoveAll(tmpDir)
		testCmdBuilder.MountPoints = append(
			testCmdBuilder.MountPoints,
			&MountPt{
				Src:      tmpDir,
				Dst:      "/tmp",
				Writable: true,
			},
		)

		// Construct the sandbox's environment by forwarding the current env
		// but overriding the TempDirEnvVars with /tmp.
		// Also override FUCHSIA_TEST_OUTDIR with the outdir specific to this
		// test.
		envOverrides := map[string]string{
			"TMPDIR":                   "/tmp",
			constants.TestOutDirEnvKey: outDir,
			llvmProfileEnvKey:          filepath.Join(profileAbsDir, "%m"+llvmProfileExtension),
		}
		for _, key := range environment.TempDirEnvVars() {
			envOverrides[key] = "/tmp"
		}
		testCmdBuilder.ForwardEnv(envOverrides)

		// Set the root of the NsJail and the working directory.
		// The working directory is expected to be a subdirectory of the root.
		if t.sProps.nsjailRoot != "" {
			absRoot, err := filepath.Abs(t.sProps.nsjailRoot)
			if err != nil {
				testResult.FailReason = err.Error()
				return testResult, nil
			}
			testCmdBuilder.MountPoints = append(
				testCmdBuilder.MountPoints,
				&MountPt{Src: absRoot, Writable: true},
			)
		}
		testCmdBuilder.Cwd = t.sProps.cwd

		// Mount the testbed config and any serial sockets.
		testbedConfigPath := os.Getenv(botanistconstants.TestbedConfigEnvKey)
		if testbedConfigPath != "" {
			// Mount the actual config.
			testCmdBuilder.MountPoints = append(testCmdBuilder.MountPoints, &MountPt{Src: testbedConfigPath})

			// Mount the SSH keys and serial sockets for each target in the testbed.
			type targetInfo struct {
				SerialSocket string `json:"serial_socket"`
				SSHKey       string `json:"ssh_key"`
			}
			b, err := os.ReadFile(testbedConfigPath)
			if err != nil {
				testResult.FailReason = err.Error()
				return testResult, nil
			}
			var testbedConfig []targetInfo
			if err := json.Unmarshal(b, &testbedConfig); err != nil {
				testResult.FailReason = err.Error()
				return testResult, nil
			}
			serialSockets := make(map[string]struct{})
			sshKeys := make(map[string]struct{})
			for _, config := range testbedConfig {
				if config.SSHKey != "" {
					sshKeys[config.SSHKey] = struct{}{}
				}
				if config.SerialSocket != "" {
					serialSockets[config.SerialSocket] = struct{}{}
				}
			}
			for socket := range serialSockets {
				absSocketPath, err := filepath.Abs(socket)
				if err != nil {
					testResult.FailReason = err.Error()
					return testResult, nil
				}
				testCmdBuilder.MountPoints = append(testCmdBuilder.MountPoints, &MountPt{
					Src:      absSocketPath,
					Writable: true,
				})
			}
			for key := range sshKeys {
				absKeyPath, err := filepath.Abs(key)
				if err != nil {
					testResult.FailReason = err.Error()
					return testResult, nil
				}
				testCmdBuilder.MountPoints = append(testCmdBuilder.MountPoints, &MountPt{
					Src: absKeyPath,
				})
			}
		}
		testCmdBuilder.AddDefaultMounts()
		testCmd, err = testCmdBuilder.Build(testCmd)
		if err != nil {
			testResult.FailReason = err.Error()
			return testResult, nil
		}
	}
	err := r.Run(ctx, testCmd, stdout, stderr)
	if err == nil {
		testResult.Result = runtests.TestSuccess
	} else if errors.Is(err, context.DeadlineExceeded) {
		testResult.Result = runtests.TestAborted
	} else {
		testResult.FailReason = err.Error()
	}

	var sinks []runtests.DataSink
	profileErr := filepath.WalkDir(profileAbsDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !d.IsDir() {
			profileRel, err := filepath.Rel(profileAbsDir, path)
			if err != nil {
				return err
			}
			sinks = append(sinks, runtests.DataSink{
				Name: filepath.Base(path),
				File: filepath.Join(profileRelDir, profileRel),
			})
		}
		return nil
	})
	if profileErr != nil {
		logger.Errorf(ctx, "unable to determine whether profiles were emitted: %s", profileErr)
	}
	if len(sinks) > 0 {
		testResult.DataSinks.Sinks = runtests.DataSinkMap{
			llvmProfileSinkType: sinks,
		}
	}
	return testResult, nil
}

func (t *SubprocessTester) EnsureSinks(ctx context.Context, sinkRefs []runtests.DataSinkReference, _ *TestOutputs) error {
	// Nothing to actually copy; if any profiles were emitted, they would have
	// been written directly to the output directory. We verify here that all
	// recorded data sinks are actually present.
	numSinks := 0
	for _, ref := range sinkRefs {
		for _, sinks := range ref.Sinks {
			for _, sink := range sinks {
				abs := filepath.Join(t.localOutputDir, sink.File)
				exists, err := osmisc.FileExists(abs)
				if err != nil {
					return fmt.Errorf("unable to determine if local data sink %q exists: %w", sink.File, err)
				} else if !exists {
					return fmt.Errorf("expected a local data sink %q, but no such file exists", sink.File)
				}
				numSinks++
			}
		}
	}
	if numSinks > 0 {
		logger.Debugf(ctx, "local data sinks present: %d", numSinks)
	}
	return nil
}

func (t *SubprocessTester) RunSnapshot(_ context.Context, _ string) error {
	return nil
}

func (t *SubprocessTester) Close() error {
	return nil
}

type serialSocket struct {
	socketPath string
}

func (s *serialSocket) runDiagnostics(ctx context.Context) error {
	if s.socketPath == "" {
		return fmt.Errorf("serialSocketPath not set")
	}
	socket, err := serial.NewSocket(ctx, s.socketPath)
	if err != nil {
		return fmt.Errorf("newSerialSocket failed: %w", err)
	}
	defer socket.Close()
	return serial.RunDiagnostics(ctx, socket)
}

// for testability
type FFXInstance interface {
	SetStdoutStderr(stdout, stderr io.Writer)
	Test(ctx context.Context, tests build.TestList, outDir string, args ...string) (*ffxutil.TestRunResult, error)
	Snapshot(ctx context.Context, outDir string, snapshotFilename string) error
	Stop() error
}

// FFXTester uses ffx to run tests and other enabled features.
type FFXTester struct {
	ffx             FFXInstance
	experimentLevel int
	// It will temporarily use an sshTester for functions where ffx has not been
	// enabled to run yet.
	// TODO(ihuh): Remove once all v1 tests are migrated to v2 and data sinks are
	// available as an output artifact of `ffx test`.
	sshTester      Tester
	localOutputDir string

	// The test output dirs from all the calls to Test().
	testOutDirs []string
}

// NewFFXTester returns an FFXTester.
func NewFFXTester(ffx FFXInstance, sshTester Tester, localOutputDir string, experimentLevel int) *FFXTester {
	return &FFXTester{
		ffx:             ffx,
		sshTester:       sshTester,
		localOutputDir:  localOutputDir,
		experimentLevel: experimentLevel,
	}
}

func (t *FFXTester) EnabledForTest(test testsharder.Test) bool {
	return test.IsComponentV2()
}

func (t *FFXTester) Test(ctx context.Context, test testsharder.Test, stdout, stderr io.Writer, outDir string) (*TestResult, error) {
	if t.EnabledForTest(test) {
		baseTestResult := BaseTestResultFromTest(test)
		testResults, err := t.TestMultiple(ctx, []testsharder.Test{test}, stdout, stderr, outDir)
		if err != nil {
			baseTestResult.FailReason = err.Error()
			return baseTestResult, nil
		}
		if len(testResults) != 1 {
			baseTestResult.FailReason = fmt.Sprintf("expected 1 test result, got %d", len(testResults))
			return baseTestResult, nil
		}
		return testResults[0], nil
	}
	return t.sshTester.Test(ctx, test, stdout, stderr, outDir)
}

// TestMultiple runs `ffx test` with multiple tests in one invocation and stores the results in lastTestResults.
func (t *FFXTester) TestMultiple(ctx context.Context, tests []testsharder.Test, stdout, stderr io.Writer, outDir string) ([]*TestResult, error) {
	var testDefs []build.TestListEntry
	testsByURL := make(map[string]testsharder.Test)
	for _, test := range tests {
		var numRuns int
		if test.RunAlgorithm == testsharder.KeepGoing {
			numRuns = test.Runs
		} else {
			// StopOnFailure and StopOnSuccess are used to determine retries, which are not
			// supported yet with ffx. Retries are now run at the end after all tests have
			// run, so they can just be rerun with another call to Test() for StopOnSuccess.
			// StopOnFailure is used for multiplier shards to run as many times as test.Runs
			// or until it gets a failure. This will need to be supported in ffx so that we
			// can use the multiple test feature in ffx to run multiplier tests.
			numRuns = 1
		}
		testsByURL[test.PackageURL] = test

		for i := 0; i < numRuns; i++ {
			testDefs = append(testDefs, build.TestListEntry{
				Name:   test.PackageURL,
				Labels: []string{test.Label},
				Execution: build.ExecutionDef{
					Type:            "fuchsia_component",
					ComponentURL:    test.PackageURL,
					TimeoutSeconds:  int(test.Timeout.Seconds()),
					Parallel:        test.Parallel,
					MaxSeverityLogs: test.LogSettings.MaxSeverity,
				},
				Tags: test.Tags,
			})
		}
	}
	t.ffx.SetStdoutStderr(stdout, stderr)
	defer t.ffx.SetStdoutStderr(os.Stdout, os.Stderr)

	extraArgs := []string{"--filter-ansi"}
	if t.experimentLevel == 3 {
		extraArgs = append(extraArgs, "--experimental-parallel-execution", "8")
	}
	runResult, err := t.ffx.Test(ctx, build.TestList{Data: testDefs, SchemaID: build.TestListSchemaIDExperimental}, outDir, extraArgs...)
	if err != nil {
		return []*TestResult{}, err
	}
	return t.processTestResult(runResult, testsByURL)
}

func (t *FFXTester) processTestResult(runResult *ffxutil.TestRunResult, testsByURL map[string]testsharder.Test) ([]*TestResult, error) {
	var testResults []*TestResult
	if runResult == nil {
		return testResults, fmt.Errorf("no test result was found")
	}
	testOutDir := runResult.GetTestOutputDir()
	t.testOutDirs = append(t.testOutDirs, testOutDir)
	suiteResults, err := runResult.GetSuiteResults()
	if err != nil {
		return testResults, err
	}

	for _, suiteResult := range suiteResults {
		test := testsByURL[suiteResult.Name]
		testResult := BaseTestResultFromTest(test)

		switch suiteResult.Outcome {
		case ffxutil.TestPassed:
			testResult.Result = runtests.TestSuccess
		case ffxutil.TestTimedOut:
			testResult.Result = runtests.TestAborted
		case ffxutil.TestNotStarted:
			testResult.Result = runtests.TestSkipped
		default:
			testResult.Result = runtests.TestFailure
		}

		var suiteArtifacts []string
		var stdioPath string
		suiteArtifactDir := filepath.Join(testOutDir, suiteResult.ArtifactDir)
		for artifact, metadata := range suiteResult.Artifacts {
			if metadata.ArtifactType == ffxutil.ReportType {
				// Copy the report log into the filename expected by infra.
				// TODO(fxbug.dev/91013): Remove dependencies on this filename.
				absPath := filepath.Join(suiteArtifactDir, artifact)
				stdioPath = filepath.Join(suiteArtifactDir, runtests.TestOutputFilename)
				if err := os.Rename(absPath, stdioPath); err != nil {
					return testResults, err
				}
				suiteArtifacts = append(suiteArtifacts, runtests.TestOutputFilename)
			} else {
				suiteArtifacts = append(suiteArtifacts, artifact)
			}
		}
		testResult.OutputFiles = suiteArtifacts
		testResult.OutputDir = suiteArtifactDir

		var cases []runtests.TestCaseResult
		for _, testCase := range suiteResult.Cases {
			var status runtests.TestResult
			switch testCase.Outcome {
			case ffxutil.TestPassed:
				status = runtests.TestSuccess
			case ffxutil.TestSkipped:
				status = runtests.TestSkipped
			default:
				status = runtests.TestFailure
			}

			var artifacts []string
			var failReason string
			testCaseArtifactDir := filepath.Join(testOutDir, testCase.ArtifactDir)
			for artifact, metadata := range testCase.Artifacts {
				// Get the failReason from the stderr log.
				// TODO(ihuh): The stderr log may contain unsymbolized logs.
				// Consider symbolizing them within ffx or testrunner.
				if metadata.ArtifactType == ffxutil.StderrType {
					stderrBytes, err := os.ReadFile(filepath.Join(testCaseArtifactDir, artifact))
					if err != nil {
						failReason = fmt.Sprintf("failed to read stderr for test case %s: %s", testCase.Name, err)
					} else {
						failReason = string(stderrBytes)
					}
				}
				artifacts = append(artifacts, artifact)
			}
			cases = append(cases, runtests.TestCaseResult{
				DisplayName: testCase.Name,
				CaseName:    testCase.Name,
				Status:      status,
				FailReason:  failReason,
				Format:      "FTF",
				OutputFiles: artifacts,
				OutputDir:   testCaseArtifactDir,
			})
		}
		testResult.Cases = cases

		testResult.StartTime = time.UnixMilli(suiteResult.StartTime)
		testResult.EndTime = time.UnixMilli(suiteResult.StartTime + suiteResult.DurationMilliseconds)
		testResults = append(testResults, testResult)
	}
	return testResults, nil
}

// RemoveAllOutputDirs removes the test output directories for all calls to Test().
func (t *FFXTester) RemoveAllOutputDirs() error {
	var errs []string
	for _, outDir := range t.testOutDirs {
		if err := os.RemoveAll(outDir); err != nil {
			errs = append(errs, fmt.Sprintf("failed to remove %s: %s", outDir, err))
		}
	}
	return fmt.Errorf(strings.Join(errs, "; "))
}

// RemoveAllEmptyOutputDirs cleans up the output dirs by removing all empty
// directories. This leaves the run_summary and suite_summaries for debugging.
func (t *FFXTester) RemoveAllEmptyOutputDirs() error {
	var errs []string
	for _, outDir := range t.testOutDirs {
		err := filepath.WalkDir(outDir, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if d.IsDir() {
				files, err := os.ReadDir(path)
				if err != nil {
					return err
				}
				if len(files) == 0 {
					if err := os.RemoveAll(path); err != nil {
						return fmt.Errorf("failed to remove %s: %s", path, err)
					}
					return filepath.SkipDir
				}
			}
			return nil
		})
		if err != nil {
			errs = append(errs, fmt.Sprintf("%v", err))
		}
	}
	return fmt.Errorf(strings.Join(errs, "; "))
}

func (t *FFXTester) Close() error {
	t.sshTester.Close()
	return t.ffx.Stop()
}

func (t *FFXTester) EnsureSinks(ctx context.Context, sinks []runtests.DataSinkReference, outputs *TestOutputs) error {
	sinksPerTest := make(map[string]runtests.DataSinkReference)
	for _, testOutDir := range t.testOutDirs {
		runResult, err := ffxutil.GetRunResult(testOutDir)
		if err != nil {
			return err
		}
		runArtifactDir := filepath.Join(testOutDir, runResult.ArtifactDir)
		seen := make(map[string]struct{})
		startTime := clock.Now(ctx)
		// The runResult's artifacts should contain a directory with the profiles from
		// component v2 tests along with a summary.json that lists the data sinks per test.
		// It should also contain a second directory with early boot data sinks.
		for artifact := range runResult.Artifacts {
			artifactPath := filepath.Join(runArtifactDir, artifact)
			if err := t.getSinks(artifactPath, sinksPerTest, seen); err != nil {
				return err
			}
		}
		copyDuration := clock.Now(ctx).Sub(startTime)
		if len(seen) > 0 {
			logger.Debugf(ctx, "copied %d data sinks in %s", len(seen), copyDuration)
		}
	}
	// If there were early boot sinks, record the "early_boot_sinks" test in the outputs
	// so that the test result can be updated with the early boot sinks.
	if _, ok := sinksPerTest[earlyBootSinksTestName]; ok {
		earlyBootSinksTest := &TestResult{
			Name:   earlyBootSinksTestName,
			Result: runtests.TestSuccess,
		}
		outputs.Record(ctx, *earlyBootSinksTest)
	}
	if len(sinksPerTest) > 0 {
		outputs.updateDataSinks(sinksPerTest, "v2")
	}
	// Copy v1 sinks.
	if sshTester, ok := t.sshTester.(*FuchsiaSSHTester); ok {
		return sshTester.copySinks(ctx, sinks, t.localOutputDir)
	}
	return nil
}

func (t *FFXTester) getSinks(artifactDir string, sinksPerTest map[string]runtests.DataSinkReference, seen map[string]struct{}) error {
	return filepath.WalkDir(artifactDir, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		// If the path is a directory, check for the summary.json and copy all
		// profiles to the localOutputDir. If the directory does not have a
		// summary.json, that means it doesn't contain profile artifacts, so ignore
		// it.
		if d.IsDir() {
			summaryPath := filepath.Join(path, runtests.TestSummaryFilename)
			f, err := os.Open(summaryPath)
			if os.IsNotExist(err) {
				return nil
			}
			if err != nil {
				return err
			}
			defer f.Close()

			var summary runtests.TestSummary
			if err = json.NewDecoder(f).Decode(&summary); err != nil {
				return fmt.Errorf("failed to read test summary from %q: %w", summaryPath, err)
			}
			if err := t.getSinksPerTest(path, summary, sinksPerTest, seen); err != nil {
				return err
			}
			return filepath.SkipDir
		}
		// Else, if the path is a .profraw file, then it must be an early boot profile.
		if filepath.Ext(path) == ".profraw" {
			return t.getEarlyBootSink(path, sinksPerTest, seen)
		}
		return nil
	})
}

// getSinksPerTest moves sinks from sinkDir to the localOutputDir and records
// the sinks in sinksPerTest.
func (t *FFXTester) getSinksPerTest(sinkDir string, summary runtests.TestSummary, sinksPerTest map[string]runtests.DataSinkReference, seen map[string]struct{}) error {
	for _, details := range summary.Tests {
		for _, sinks := range details.DataSinks {
			for _, sink := range sinks {
				if _, ok := seen[sink.File]; !ok {
					newPath := filepath.Join(t.localOutputDir, "v2", sink.File)
					if err := os.MkdirAll(filepath.Dir(newPath), os.ModePerm); err != nil {
						return err
					}
					if err := os.Rename(filepath.Join(sinkDir, sink.File), newPath); err != nil {
						return err
					}
					seen[sink.File] = struct{}{}
				}
			}
		}
		sinksPerTest[details.Name] = runtests.DataSinkReference{Sinks: details.DataSinks}
	}
	return nil
}

// getEarlyBootSink moves the early boot sink to the localOutputDir and records it with
// an "early_boot_sinks" test in sinksPerTest.
func (t *FFXTester) getEarlyBootSink(path string, sinksPerTest map[string]runtests.DataSinkReference, seen map[string]struct{}) error {
	sinkFile := filepath.Base(path)
	if _, ok := seen[path]; !ok {
		newPath := filepath.Join(t.localOutputDir, "v2", sinkFile)
		if err := os.MkdirAll(filepath.Dir(newPath), os.ModePerm); err != nil {
			return err
		}
		if err := os.Rename(path, newPath); err != nil {
			return err
		}
		seen[path] = struct{}{}
	}
	earlyBootSinks, ok := sinksPerTest[earlyBootSinksTestName]
	if !ok {
		earlyBootSinks = runtests.DataSinkReference{Sinks: runtests.DataSinkMap{}}
	}
	earlyBootSinks.Sinks["llvm-profile"] = append(earlyBootSinks.Sinks["llvm-profile"], runtests.DataSink{Name: sinkFile, File: sinkFile})
	sinksPerTest[earlyBootSinksTestName] = earlyBootSinks
	return nil
}

func (t *FFXTester) RunSnapshot(ctx context.Context, snapshotFile string) error {
	if snapshotFile == "" {
		return nil
	}
	startTime := clock.Now(ctx)
	const maxReconnectAttempts = 3
	err := retry.Retry(ctx, retry.WithMaxAttempts(retry.NewConstantBackoff(time.Second), maxReconnectAttempts), func() error {
		return t.ffx.Snapshot(ctx, t.localOutputDir, snapshotFile)
	}, nil)
	if err != nil {
		logger.Errorf(ctx, "%s: %s", constants.FailedToRunSnapshotMsg, err)
	}
	logger.Debugf(ctx, "ran snapshot in %s", clock.Now(ctx).Sub(startTime))
	return err
}

func sshToTarget(ctx context.Context, addr net.IPAddr, sshKeyFile string) (*sshutil.Client, error) {
	key, err := os.ReadFile(sshKeyFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read SSH key file: %w", err)
	}
	config, err := sshutil.DefaultSSHConfig(key)
	if err != nil {
		return nil, fmt.Errorf("failed to create an SSH client config: %w", err)
	}

	return sshutil.NewClient(
		ctx,
		sshutil.ConstantAddrResolver{
			Addr: &net.TCPAddr{
				IP:   addr.IP,
				Port: sshutil.SSHPort,
				Zone: addr.Zone,
			},
		},
		config,
		sshutil.DefaultConnectBackoff(),
	)
}

// FuchsiaSSHTester executes fuchsia tests over an SSH connection.
type FuchsiaSSHTester struct {
	client                      sshClient
	copier                      dataSinkCopier
	useRuntests                 bool
	localOutputDir              string
	connectionErrorRetryBackoff retry.Backoff
	serialSocket                serialClient
}

// NewFuchsiaSSHTester returns a FuchsiaSSHTester associated to a fuchsia
// instance of given nodename, the private key paired with an authorized one
// and the directive of whether `runtests` should be used to execute the test.
func NewFuchsiaSSHTester(ctx context.Context, addr net.IPAddr, sshKeyFile, localOutputDir, serialSocketPath string, useRuntests bool) (Tester, error) {
	client, err := sshToTarget(ctx, addr, sshKeyFile)
	if err != nil {
		return nil, fmt.Errorf("failed to establish an SSH connection: %w", err)
	}
	copier, err := runtests.NewDataSinkCopier(client)
	if err != nil {
		return nil, err
	}
	return &FuchsiaSSHTester{
		client:                      client,
		copier:                      copier,
		useRuntests:                 useRuntests,
		localOutputDir:              localOutputDir,
		connectionErrorRetryBackoff: retry.NewConstantBackoff(time.Second),
		serialSocket:                &serialSocket{serialSocketPath},
	}, nil
}

func (t *FuchsiaSSHTester) reconnect(ctx context.Context) error {
	if err := t.client.Reconnect(ctx); err != nil {
		return fmt.Errorf("failed to reestablish SSH connection: %w", err)
	}
	if err := t.copier.Reconnect(); err != nil {
		return fmt.Errorf("failed to reconnect data sink copier: %w", err)
	}
	return nil
}

// sshExitError is an interface that ssh.ExitError conforms to. We use this for
// testability instead of unwrapping an error as an ssh.ExitError, because it's
// not possible to construct an ssh.ExitError in-memory in a test due to private
// field constraints.
type sshExitError interface {
	error
	ExitStatus() int
}

// Statically assert that ssh.ExitError implements the sshExitError interface.
var _ sshExitError = &ssh.ExitError{}

func (t *FuchsiaSSHTester) isTimeoutError(test testsharder.Test, err error) bool {
	if test.Timeout <= 0 {
		return false
	}
	var exitErr sshExitError
	if errors.As(err, &exitErr) {
		return exitErr.ExitStatus() == timeoutExitCode
	}
	return false
}

func (t *FuchsiaSSHTester) runSSHCommandWithRetry(ctx context.Context, command []string, stdout, stderr io.Writer) error {
	const maxReconnectAttempts = 3
	return retry.Retry(ctx, retry.WithMaxAttempts(t.connectionErrorRetryBackoff, maxReconnectAttempts), func() error {
		if cmdErr := t.client.Run(ctx, command, stdout, stderr); cmdErr != nil {
			if !sshutil.IsConnectionError(cmdErr) {
				// Not a connection error -> break retry loop.
				return retry.Fatal(cmdErr)
			}
			logger.Errorf(ctx, "attempting to reconnect over SSH after error: %s", cmdErr)
			if err := t.reconnect(ctx); err != nil {
				logger.Errorf(ctx, "%s: %s", constants.FailedToReconnectMsg, err)
				// If we fail to reconnect, continuing is likely hopeless.
				// Return the *original* error (which will generally be more
				// closely related to the root cause of the failure) rather than
				// the reconnection error.
				return retry.Fatal(cmdErr)
			}
			// Return non-ConnectionError because code in main.go will exit
			// early if it sees that. Since reconnection succeeded, we don't
			// want that.
			// TODO(olivernewman): Clean this up; have main.go do its own
			// connection recovery between tests.
			cmdErr = fmt.Errorf("%s", cmdErr)
			return cmdErr
		}
		return nil
	}, nil)
}

// Test runs a test over SSH.
func (t *FuchsiaSSHTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer, _ string) (*TestResult, error) {
	testResult := BaseTestResultFromTest(test)
	command, err := commandForTest(&test, t.useRuntests, dataOutputDir, test.Timeout)
	if err != nil {
		testResult.FailReason = err.Error()
		return testResult, nil
	}
	testErr := t.runSSHCommandWithRetry(ctx, command, stdout, stderr)
	if testErr == nil {
		testResult.Result = runtests.TestSuccess
	}

	if sshutil.IsConnectionError(testErr) {
		if err := t.serialSocket.runDiagnostics(ctx); err != nil {
			logger.Warningf(ctx, "failed to run serial diagnostics: %s", err)
		}
		// If we continue to experience a connection error after several retries
		// then the device has likely become unresponsive and there's no use in
		// continuing to try to run tests, so mark the error as fatal.
		return nil, testErr
	}

	if t.isTimeoutError(test, testErr) {
		testResult.Result = runtests.TestAborted
	} else if testErr != nil {
		testResult.FailReason = testErr.Error()
	} else {
		testResult.Result = runtests.TestSuccess
	}

	var sinkErr error
	if t.useRuntests && !test.IsComponentV2() {
		startTime := clock.Now(ctx)
		var sinksPerTest map[string]runtests.DataSinkReference
		if sinksPerTest, sinkErr = t.copier.GetReferences(dataOutputDir); sinkErr != nil {
			logger.Errorf(ctx, "failed to determine data sinks for test %q: %s", test.Name, sinkErr)
		} else {
			testResult.DataSinks = sinksPerTest[test.Name]
		}
		duration := clock.Now(ctx).Sub(startTime)
		if testResult.DataSinks.Size() > 0 {
			logger.Debugf(ctx, "%d data sinks found in %s", testResult.DataSinks.Size(), duration)
		}
	}

	if testErr == nil && sinkErr != nil {
		testResult.Result = runtests.TestFailure
		testResult.FailReason = sinkErr.Error()
	}
	return testResult, nil
}

func (t *FuchsiaSSHTester) EnsureSinks(ctx context.Context, sinkRefs []runtests.DataSinkReference, outputs *TestOutputs) error {
	// Collect v2 references.
	v2Sinks, err := t.copier.GetReferences(dataOutputDirV2)
	if err != nil {
		// If we fail to get v2 sinks, just log the error but continue to copy v1 sinks.
		logger.Debugf(ctx, "failed to determine data sinks for v2 tests: %s", err)
	}
	var v2SinkRefs []runtests.DataSinkReference
	for _, ref := range v2Sinks {
		v2SinkRefs = append(v2SinkRefs, ref)
	}
	if len(v2SinkRefs) > 0 {
		if err := t.copySinks(ctx, v2SinkRefs, filepath.Join(t.localOutputDir, "v2")); err != nil {
			return err
		}
		outputs.updateDataSinks(v2Sinks, "v2")
	}
	// Collect early boot coverage.
	earlyBootSinks, err := t.copier.GetAllDataSinks(dataOutputDirEarlyBoot)
	if err != nil {
		logger.Debugf(ctx, "failed to determine early boot data sinks: %s", err)
	}
	if len(earlyBootSinks) > 0 {
		// If there were early boot sinks, record the "early_boot_sinks" test in the outputs
		// so that the test result can be updated with the early boot sinks.
		earlyBootSinksTest := &TestResult{
			Name:   earlyBootSinksTestName,
			Result: runtests.TestSuccess,
		}
		outputs.Record(ctx, *earlyBootSinksTest)
		earlyBootSinkRef := runtests.DataSinkReference{Sinks: runtests.DataSinkMap{"llvm-profile": earlyBootSinks}, RemoteDir: dataOutputDirEarlyBoot}
		if err := t.copySinks(ctx, []runtests.DataSinkReference{earlyBootSinkRef}, filepath.Join(t.localOutputDir, "early-boot")); err != nil {
			return err
		}
		outputs.updateDataSinks(map[string]runtests.DataSinkReference{earlyBootSinksTestName: earlyBootSinkRef}, "early-boot")
	}
	return t.copySinks(ctx, sinkRefs, t.localOutputDir)
}

func (t *FuchsiaSSHTester) copySinks(ctx context.Context, sinkRefs []runtests.DataSinkReference, localOutputDir string) error {
	strategy := retry.WithMaxAttempts(retry.NewConstantBackoff(time.Second), 4)
	disconnected := false
	if err := retry.Retry(ctx, strategy, func() error {
		if disconnected {
			logger.Debugf(ctx, "reconnecting to retry downloading data sinks...")
			if err := t.reconnect(ctx); err != nil {
				return retry.Fatal(err)
			}
			disconnected = false
			logger.Debugf(ctx, "successfully reconnected, will retry downloading data sinks")
		}
		startTime := clock.Now(ctx)

		// Copy() is assumed to be idempotent and thus safe to retry, which is
		// the case for the SFTP-based data sink copier.
		sinkMap, err := t.copier.Copy(sinkRefs, localOutputDir)
		if err != nil {
			if errors.Is(err, sftp.ErrSSHFxConnectionLost) {
				logger.Warningf(ctx, "connection lost while downlading data sinks: %s", err)
				disconnected = true
				return err
			}
			return retry.Fatal(err)
		}
		copyDuration := clock.Now(ctx).Sub(startTime)
		sinkRef := runtests.DataSinkReference{Sinks: sinkMap}
		numSinks := sinkRef.Size()
		if numSinks > 0 {
			logger.Debugf(ctx, "copied %d data sinks in %s", numSinks, copyDuration)
		}
		return nil
	}, nil); err != nil {
		return fmt.Errorf("failed to copy data sinks off target: %w", err)
	}
	return nil
}

// RunSnapshot runs `snapshot` on the device.
func (t *FuchsiaSSHTester) RunSnapshot(ctx context.Context, snapshotFile string) error {
	if snapshotFile == "" {
		return nil
	}
	startTime := clock.Now(ctx)
	snapshotOutFile, err := osmisc.CreateFile(filepath.Join(t.localOutputDir, snapshotFile))
	if err != nil {
		return fmt.Errorf("failed to create snapshot output file: %w", err)
	}
	defer snapshotOutFile.Close()
	if err := t.runSSHCommandWithRetry(ctx, []string{"/bin/snapshot"}, snapshotOutFile, os.Stderr); err != nil {
		logger.Errorf(ctx, "%s: %s", constants.FailedToRunSnapshotMsg, err)
		if sshutil.IsConnectionError(err) {
			if err := t.serialSocket.runDiagnostics(ctx); err != nil {
				logger.Warningf(ctx, "failed to run serial diagnostics: %s", err)
			}
		}
	}
	logger.Debugf(ctx, "ran snapshot in %s", clock.Now(ctx).Sub(startTime))
	return err
}

// Close terminates the underlying SSH connection. The object is no longer
// usable after calling this method.
func (t *FuchsiaSSHTester) Close() error {
	defer t.client.Close()
	return t.copier.Close()
}

// for testability
type socketConn interface {
	io.ReadWriteCloser
	SetIOTimeout(timeout time.Duration)
}

// FuchsiaSerialTester executes fuchsia tests over serial.
type FuchsiaSerialTester struct {
	socket         socketConn
	localOutputDir string
}

// NewFuchsiaSerialTester creates a tester that runs tests over serial.
func NewFuchsiaSerialTester(ctx context.Context, serialSocketPath string) (Tester, error) {
	socket, err := serial.NewSocket(ctx, serialSocketPath)
	if err != nil {
		return nil, err
	}
	return &FuchsiaSerialTester{socket: socket}, nil
}

// Exposed for testability.
var newTestStartedContext = func(ctx context.Context) (context.Context, context.CancelFunc) {
	return context.WithTimeout(ctx, testStartedTimeout)
}

// lastWriteSaver is an io.Writer that saves the bytes written in the last Write().
type lastWriteSaver struct {
	buf []byte
}

func (w *lastWriteSaver) Write(p []byte) (int, error) {
	w.buf = make([]byte, len(p))
	copy(w.buf, p)
	return len(p), nil
}

// parseOutKernelReader is an io.Reader that reads from the underlying reader
// everything not pertaining to a kernel log. A kernel log is distinguished by
// a line that starts with the timestamp represented as a float inside brackets.
type parseOutKernelReader struct {
	ctx    context.Context
	reader io.Reader
	// unprocessed stores the last characters read from a Read() but not returned
	// by it. This could happen if we read more than necessary to try to complete
	// a possible kernel log and cannot return all of the bytes. This will be
	// read in the next call to Read().
	unprocessed []byte
	// kernelLineStart stores the last characters read from a Read() block if it
	// ended with a truncated line and possibly contains a kernel log. This will
	// be prepended to the next Read() block.
	kernelLineStart []byte
	reachedEOF      bool
}

func (r *parseOutKernelReader) Read(buf []byte) (int, error) {
	// If the underlying reader already reached EOF, that means kernelLineStart is
	// not the start of a kernel log, so append it to unprocessed to be read normally.
	if r.reachedEOF {
		r.unprocessed = append(r.unprocessed, r.kernelLineStart...)
		r.kernelLineStart = []byte{}
	}
	// If there are any unprocessed bytes, read them first instead of calling the
	// underlying reader's Read() again.
	if len(r.unprocessed) > 0 {
		bytesToRead := int(math.Min(float64(len(buf)), float64(len(r.unprocessed))))
		copy(buf, r.unprocessed[:bytesToRead])
		r.unprocessed = r.unprocessed[bytesToRead:]
		return bytesToRead, nil
	} else if r.reachedEOF {
		// r.unprocessed was empty so we can just return EOF.
		return 0, io.EOF
	}

	if r.ctx.Err() != nil {
		return 0, r.ctx.Err()
	}

	b := make([]byte, len(buf))
	type readResult struct {
		n   int
		err error
	}
	ch := make(chan readResult, 1)
	// Call the underlying reader's Read() in a goroutine so that we can
	// break out if the context is canceled.
	go func() {
		readN, readErr := r.reader.Read(b)
		ch <- readResult{readN, readErr}
	}()
	var n int
	var err error
	select {
	case res := <-ch:
		n = res.n
		err = res.err
		break
	case <-r.ctx.Done():
		err = r.ctx.Err()
	}

	if err != nil && err != io.EOF {
		return n, err
	}
	// readBlock contains everything stored in kernelLineStart (bytes last read
	// from the underlying reader in the previous Read() call that possibly contain
	// a truncated kernel log that has not been processed by this reader yet) along
	// with the new bytes just read. Because readBlock contains unprocessed bytes,
	// its length will likely be greater than len(buf).
	// However, it is necessary to read more bytes in the case that the unprocessed
	// bytes contain a long truncated kernel log and we need to keep reading more
	// bytes until we get to the end of the line so we can discard it.
	readBlock := append(r.kernelLineStart, b[:n]...)
	r.kernelLineStart = []byte{}
	lines := bytes.Split(readBlock, []byte("\n"))
	var bytesRead, bytesLeftToRead int
	for i, line := range lines {
		bytesLeftToRead = len(buf) - bytesRead
		isTruncated := i == len(lines)-1
		line = r.lineWithoutKernelLog(line, isTruncated)
		if bytesLeftToRead == 0 {
			// If there are no more bytes left to read, store the rest of the lines
			// into r.unprocessed to be read at the next call to Read().
			r.unprocessed = append(r.unprocessed, line...)
			continue
		}
		if len(line) > bytesLeftToRead {
			// If the line is longer than bytesLeftToRead, read as much as possible
			// and store the rest in r.unprocessed.
			copy(buf[bytesRead:], line[:bytesLeftToRead])
			r.unprocessed = line[bytesLeftToRead:]
			bytesRead += bytesLeftToRead
		} else {
			copy(buf[bytesRead:bytesRead+len(line)], line)
			bytesRead += len(line)
		}
	}
	if err == io.EOF {
		r.reachedEOF = true
	}
	if len(r.unprocessed)+len(r.kernelLineStart) > 0 {
		err = nil
	}
	return bytesRead, err
}

func (r *parseOutKernelReader) lineWithoutKernelLog(line []byte, isTruncated bool) []byte {
	containsKernelLog := false
	re := regexp.MustCompile(`\[[0-9]+\.?[0-9]+\]`)
	match := re.FindIndex(line)
	if match != nil {
		if isTruncated {
			r.kernelLineStart = line[match[0]:]
		}
		// The new line to add to bytes read contains everything in the line up to
		// the bracket indicating the kernel log.
		line = line[:match[0]]
		containsKernelLog = true
	} else if isTruncated {
		// Match the beginning of a possible kernel log timestamp.
		// i.e. `[`, `[123` `[123.4`
		re = regexp.MustCompile(`\[[0-9]*\.?[0-9]*$`)
		match = re.FindIndex(line)
		if match != nil {
			r.kernelLineStart = line[match[0]:]
			line = line[:match[0]]
		}
	}
	if !containsKernelLog && !isTruncated {
		line = append(line, '\n')
	}
	return line
}

func (t *FuchsiaSerialTester) Test(ctx context.Context, test testsharder.Test, stdout, _ io.Writer, _ string) (*TestResult, error) {
	testResult := BaseTestResultFromTest(test)
	command, err := commandForTest(&test, true, "", test.Timeout)
	if err != nil {
		testResult.FailReason = err.Error()
		return testResult, nil
	}
	logger.Debugf(ctx, "starting: %s", command)

	// TODO(fxbug.dev/86771): Currently, serial output is coming out jumbled,
	// so the started string sometimes comes after the completed string, resulting
	// in a timeout because we fail to read the completed string after the
	// started string. Uncomment below to use the lastWriteSaver once the bug is
	// fixed.
	var lastWrite bytes.Buffer
	// If a single read from the socket includes both the bytes that indicate the test started and the bytes
	// that indicate the test completed, then the startedReader will consume the bytes needed for detecting
	// completion. Thus we save the last read from the socket and replay it when searching for completion.
	// lastWrite := &lastWriteSaver{}
	t.socket.SetIOTimeout(testStartedTimeout)
	reader := io.TeeReader(t.socket, &lastWrite)
	commandStarted := false
	var readErr error
	for i := 0; i < startSerialCommandMaxAttempts; i++ {
		if err := serial.RunCommands(ctx, t.socket, []serial.Command{{Cmd: command}}); err != nil {
			return nil, fmt.Errorf("failed to write to serial socket: %w", err)
		}
		startedCtx, cancel := newTestStartedContext(ctx)
		startedStr := runtests.StartedSignature + test.Name
		_, readErr = iomisc.ReadUntilMatchString(startedCtx, reader, startedStr)
		cancel()
		if readErr == nil {
			commandStarted = true
			break
		} else if errors.Is(readErr, startedCtx.Err()) {
			logger.Warningf(ctx, "test not started after timeout")
		} else {
			logger.Errorf(ctx, "unexpected error checking for test start signature: %s", readErr)
		}
	}
	if !commandStarted {
		err = fmt.Errorf("%s within %d attempts: %w",
			constants.FailedToStartSerialTestMsg, startSerialCommandMaxAttempts, readErr)
		// In practice, repeated failure to run a test means that the device has
		// become unresponsive and we won't have any luck running later tests.
		return nil, err
	}

	t.socket.SetIOTimeout(test.Timeout + 30*time.Second)
	testOutputReader := io.TeeReader(
		// See comment above lastWrite declaration.
		&parseOutKernelReader{ctx: ctx, reader: io.MultiReader(&lastWrite, t.socket)},
		// Writes to stdout as it reads from the above reader.
		stdout)
	if success, err := runtests.TestPassed(ctx, testOutputReader, test.Name); err != nil {
		testResult.FailReason = err.Error()
		return testResult, nil
	} else if !success {
		if errors.Is(err, io.EOF) {
			// EOF indicates that serial has become disconnected. That is
			// unlikely to be caused by this test and we're unlikely to be able
			// to keep running tests.
			return nil, err
		}
		testResult.FailReason = "test failed"
		return testResult, nil
	}
	testResult.Result = runtests.TestSuccess
	return testResult, nil
}

func (t *FuchsiaSerialTester) EnsureSinks(_ context.Context, _ []runtests.DataSinkReference, _ *TestOutputs) error {
	return nil
}

func (t *FuchsiaSerialTester) RunSnapshot(_ context.Context, _ string) error {
	return nil
}

// Close terminates the underlying Serial socket connection. The object is no
// longer usable after calling this method.
func (t *FuchsiaSerialTester) Close() error {
	return t.socket.Close()
}

func commandForTest(test *testsharder.Test, useRuntests bool, remoteOutputDir string, timeout time.Duration) ([]string, error) {
	command := []string{}
	// For v2 coverage data, use run-test-suite instead of runtests and collect the data from the designated dataOutputDirV2 directory.
	if useRuntests && !test.IsComponentV2() {
		command = []string{runtestsName}
		if remoteOutputDir != "" {
			command = append(command, "--output", remoteOutputDir)
		}
		if timeout > 0 {
			command = append(command, "-i", fmt.Sprintf("%d", int64(timeout.Seconds())))
		}
		if test.RealmLabel != "" {
			command = append(command, "--realm-label", test.RealmLabel)
		}
		if test.PackageURL != "" {
			command = append(command, test.PackageURL)
		} else {
			command = append(command, test.Path)
		}
	} else if test.PackageURL != "" {
		if test.IsComponentV2() {
			command = []string{runTestSuiteName, "--filter-ansi"}
			if test.LogSettings.MaxSeverity != "" {
				command = append(command, "--max-severity-logs", fmt.Sprintf("%s", test.LogSettings.MaxSeverity))
			}
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

			// run-test-component supports realm-label but run-test-suite does not
			if test.RealmLabel != "" {
				command = append(command, "--realm-label", test.RealmLabel)
			}
		}
		command = append(command, test.PackageURL)
	} else {
		return nil, fmt.Errorf("PackageURL is not set and useRuntests is false for %q", test.Name)
	}
	return command, nil
}
