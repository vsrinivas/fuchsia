// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"net/url"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	botanistconstants "go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/botanist/target"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/clock"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/environment"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/streams"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap"
	"go.fuchsia.dev/fuchsia/tools/testing/testparser"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
)

// Fuchsia-specific environment variables possibly exposed to the testrunner.
const (
	testTimeoutGracePeriod = 30 * time.Second
)

type testrunnerFlags struct {
	// Whether to show Usage and exit.
	help bool

	// The path where a directory containing test results should be created.
	outDir string

	// Working directory of the local testing subprocesses.
	localWD string

	// Whether to use runtests when executing tests on fuchsia. If false, the
	// default will be run_test_component.
	useRuntests bool

	// The output filename for the snapshot. This will be created in the outDir.
	snapshotFile string

	// Logger level.
	logLevel logger.LogLevel

	// The path to the ffx tool.
	ffxPath string
}

func usage() {
	fmt.Printf(`testrunner [flags] tests-file

Executes all tests found in the JSON [tests-file]
Fuchsia tests require both the node address of the fuchsia instance and a private
SSH key corresponding to a authorized key to be set in the environment under
%s and %s respectively.
`, botanistconstants.DeviceAddrEnvKey, botanistconstants.SSHKeyEnvKey)
}

func main() {
	var flags testrunnerFlags
	flags.logLevel = logger.InfoLevel // Default that may be overridden.

	flag.BoolVar(&flags.help, "help", false, "Whether to show Usage and exit.")
	flag.StringVar(&flags.outDir, "out-dir", "", "Optional path where a directory containing test results should be created.")
	flag.StringVar(&flags.localWD, "C", "", "Working directory of local testing subprocesses; if unset the current working directory will be used.")
	flag.BoolVar(&flags.useRuntests, "use-runtests", false, "Whether to default to running fuchsia tests with runtests; if false, run_test_component will be used.")
	flag.StringVar(&flags.snapshotFile, "snapshot-output", "", "The output filename for the snapshot. This will be created in the output directory.")
	flag.Var(&flags.logLevel, "level", "Output verbosity, can be fatal, error, warning, info, debug or trace.")
	flag.StringVar(&flags.ffxPath, "ffx", "", "Path to the ffx tool.")

	flag.Usage = usage
	flag.Parse()

	if flags.help || flag.NArg() != 1 {
		flag.Usage()
		flag.PrintDefaults()
		return
	}

	const logFlags = log.Ltime | log.Lmicroseconds | log.Lshortfile

	// Our mDNS library doesn't use the logger library.
	log.SetFlags(logFlags)

	log := logger.NewLogger(flags.logLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "testrunner ")
	log.SetFlags(logFlags)
	ctx := logger.WithLogger(context.Background(), log)
	ctx, cancel := signal.NotifyContext(ctx, syscall.SIGTERM, syscall.SIGINT)
	defer cancel()

	if err := setupAndExecute(ctx, flags); err != nil {
		logger.Fatalf(ctx, err.Error())
	}
}

func setupAndExecute(ctx context.Context, flags testrunnerFlags) error {
	testsPath := flag.Arg(0)
	tests, err := loadTests(testsPath)
	if err != nil {
		return fmt.Errorf("failed to load tests from %q: %w", testsPath, err)
	}

	// Configure a test outputs object, responsible for producing TAP output,
	// recording data sinks, and archiving other test outputs.
	testOutDir := filepath.Join(os.Getenv(constants.TestOutDirEnvKey), flags.outDir)
	if testOutDir == "" {
		var err error
		testOutDir, err = ioutil.TempDir("", "testrunner")
		if err != nil {
			return fmt.Errorf("failed to create a test output directory")
		}
	}
	logger.Debugf(ctx, "test output directory: %s", testOutDir)

	var addr net.IPAddr
	if deviceAddr, ok := os.LookupEnv(botanistconstants.DeviceAddrEnvKey); ok {
		addrPtr, err := net.ResolveIPAddr("ip", deviceAddr)
		if err != nil {
			return fmt.Errorf("failed to parse device address %s: %w", deviceAddr, err)
		}
		addr = *addrPtr
	}
	sshKeyFile := os.Getenv(botanistconstants.SSHKeyEnvKey)

	cleanUp, err := environment.Ensure()
	if err != nil {
		return fmt.Errorf("failed to setup environment: %w", err)
	}
	defer cleanUp()

	tapProducer := tap.NewProducer(os.Stdout)
	tapProducer.Plan(len(tests))
	outputs := testrunner.CreateTestOutputs(tapProducer, testOutDir)

	serialSocketPath := os.Getenv(botanistconstants.SerialSocketEnvKey)
	execErr := execute(ctx, tests, outputs, addr, sshKeyFile, serialSocketPath, testOutDir, flags)
	if err := outputs.Close(); err != nil {
		if execErr == nil {
			return err
		}
		logger.Warningf(ctx, "Failed to save test outputs: %s", err)
	}
	return execErr
}

func validateTest(test testsharder.Test) error {
	if test.Name == "" {
		return fmt.Errorf("one or more tests missing `name` field")
	}
	if test.OS == "" {
		return fmt.Errorf("one or more tests missing `os` field")
	}
	if test.Runs <= 0 {
		return fmt.Errorf("one or more tests with invalid `runs` field")
	}
	if test.Runs > 1 {
		switch test.RunAlgorithm {
		case testsharder.KeepGoing, testsharder.StopOnFailure, testsharder.StopOnSuccess:
		default:
			return fmt.Errorf("one or more tests with invalid `run_algorithm` field")
		}
	}
	if test.OS == "fuchsia" && test.PackageURL == "" && test.Path == "" {
		return fmt.Errorf("one or more fuchsia tests missing the `path` and `package_url` fields")
	}
	if test.OS != "fuchsia" {
		if test.PackageURL != "" {
			return fmt.Errorf("one or more host tests have a `package_url` field present")
		} else if test.Path == "" {
			return fmt.Errorf("one or more host tests missing the `path` field")
		}
	}
	return nil
}

func loadTests(path string) ([]testsharder.Test, error) {
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %w", path, err)
	}

	var tests []testsharder.Test
	if err := json.Unmarshal(bytes, &tests); err != nil {
		return nil, fmt.Errorf("failed to unmarshal %q: %w", path, err)
	}

	for _, test := range tests {
		if err := validateTest(test); err != nil {
			return nil, err
		}
	}

	return tests, nil
}

// for testability
var (
	sshTester    = testrunner.NewFuchsiaSSHTester
	serialTester = testrunner.NewFuchsiaSerialTester
)

var ffxInstance = func(ctx context.Context, ffxPath string, dir string, env []string, target, sshKey string, outputDir string) (testrunner.FFXInstance, error) {
	ffx, err := func() (testrunner.FFXInstance, error) {
		ffx, err := ffxutil.NewFFXInstance(ffxPath, dir, env, target, sshKey, outputDir)
		if ffx == nil {
			// Return nil instead of ffx so that the returned FFXTester
			// will be the nil interface instead of an interface holding
			// a nil value. In the latter case, checking ffx == nil will
			// return false.
			return nil, err
		}
		if err != nil {
			return ffx, err
		}
		// Print the list of available targets for debugging purposes.
		// TODO(ihuh): Remove when not needed.
		if err := ffx.List(ctx); err != nil {
			return ffx, err
		}
		// Wait for the target to be available to interact with ffx.
		if err := ffx.TargetWait(ctx); err != nil {
			return ffx, err
		}
		// Print the config for debugging purposes.
		// TODO(ihuh): Remove when not needed.
		if err := ffx.GetConfig(ctx); err != nil {
			return ffx, err
		}
		return ffx, nil
	}()
	if err != nil && ffx != nil {
		ffx.Stop()
	}
	return ffx, err
}

func execute(
	ctx context.Context,
	tests []testsharder.Test,
	outputs *testrunner.TestOutputs,
	addr net.IPAddr,
	sshKeyFile,
	serialSocketPath,
	outDir string,
	flags testrunnerFlags,
) error {
	var fuchsiaSinks, localSinks []runtests.DataSinkReference
	var fuchsiaTester, localTester testrunner.Tester

	localEnv := append(os.Environ(),
		// Tell tests written in Rust to print stack on failures.
		"RUST_BACKTRACE=1",
	)

	if sshKeyFile != "" {
		ffx, err := ffxInstance(
			ctx, flags.ffxPath, flags.localWD, localEnv, os.Getenv(botanistconstants.NodenameEnvKey),
			sshKeyFile, outputs.OutDir)
		if err != nil {
			return err
		}
		if ffx != nil {
			defer ffx.Stop()
			t, err := sshTester(
				ctx, addr, sshKeyFile, outputs.OutDir, serialSocketPath, flags.useRuntests)
			if err != nil {
				return fmt.Errorf("failed to initialize fuchsia tester: %w", err)
			}
			fuchsiaTester = testrunner.NewFFXTester(ffx, t, outputs.OutDir)
		}
	}

	// Function to select the tester to use for a test, along with destination
	// for the test to write any data sinks. This logic is not easily testable
	// because it requires a lot of network requests and environment inspection,
	// so we use dependency injection and pass it as a parameter to
	// `runAndOutputTests` to make that function more easily testable.
	testerForTest := func(test testsharder.Test) (testrunner.Tester, *[]runtests.DataSinkReference, error) {
		switch test.OS {
		case "fuchsia":
			if fuchsiaTester == nil {
				var err error
				if sshKeyFile != "" {
					fuchsiaTester, err = sshTester(
						ctx, addr, sshKeyFile, outputs.OutDir, serialSocketPath, flags.useRuntests)
				} else {
					if serialSocketPath == "" {
						return nil, nil, fmt.Errorf("%q must be set if %q is not set", botanistconstants.SerialSocketEnvKey, botanistconstants.SSHKeyEnvKey)
					}
					fuchsiaTester, err = serialTester(ctx, serialSocketPath)
				}
				if err != nil {
					return nil, nil, fmt.Errorf("failed to initialize fuchsia tester: %w", err)
				}
			}
			return fuchsiaTester, &fuchsiaSinks, nil
		case "linux", "mac":
			if test.OS == "linux" && runtime.GOOS != "linux" {
				return nil, nil, fmt.Errorf("cannot run linux tests when GOOS = %q", runtime.GOOS)
			}
			if test.OS == "mac" && runtime.GOOS != "darwin" {
				return nil, nil, fmt.Errorf("cannot run mac tests when GOOS = %q", runtime.GOOS)
			}
			// Initialize the fuchsia SSH tester to run the snapshot at the end in case
			// we ran any host-target interaction tests.
			if fuchsiaTester == nil && sshKeyFile != "" {
				var err error
				fuchsiaTester, err = sshTester(
					ctx, addr, sshKeyFile, outputs.OutDir, serialSocketPath, flags.useRuntests)
				if err != nil {
					logger.Errorf(ctx, "failed to initialize fuchsia tester: %s", err)
				}
			}
			if localTester == nil {
				localTester = testrunner.NewSubprocessTester(flags.localWD, localEnv, outputs.OutDir)
			}
			return localTester, &localSinks, nil
		default:
			return nil, nil, fmt.Errorf("test %#v has unsupported OS: %q", test, test.OS)
		}
	}

	var finalError error
	if err := runAndOutputTests(ctx, tests, testerForTest, outputs, outDir); err != nil {
		finalError = err
	}

	if fuchsiaTester != nil {
		defer fuchsiaTester.Close()
	}
	if localTester != nil {
		defer localTester.Close()
	}
	finalize := func(t testrunner.Tester, sinks []runtests.DataSinkReference) error {
		if t != nil {
			if err := t.RunSnapshot(ctx, flags.snapshotFile); err != nil {
				// This error usually has a different root cause that gets masked when we
				// return this error. Log it so we can keep track of it, but don't fail.
				logger.Errorf(ctx, err.Error())
			}
			if err := t.EnsureSinks(ctx, sinks, outputs); err != nil {
				return err
			}
		}
		return nil
	}

	if err := finalize(localTester, localSinks); err != nil && finalError == nil {
		finalError = err
	}
	if err := finalize(fuchsiaTester, fuchsiaSinks); err != nil && finalError == nil {
		finalError = err
	}
	return finalError
}

// stdioBuffer is a simple thread-safe wrapper around bytes.Buffer. It
// implements the io.Writer interface.
type stdioBuffer struct {
	// Used to protect access to `buf`.
	mu sync.Mutex

	// The underlying buffer.
	buf bytes.Buffer
}

func (b *stdioBuffer) Write(p []byte) (n int, err error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.buf.Write(p)
}

// runAndOutputTests runs all the tests, possibly with retries, and records the
// results to `outputs`.
func runAndOutputTests(
	ctx context.Context,
	tests []testsharder.Test,
	testerForTest func(testsharder.Test) (testrunner.Tester, *[]runtests.DataSinkReference, error),
	outputs *testrunner.TestOutputs,
	globalOutDir string,
) error {
	// testToRun represents an entry in the queue of tests to run.
	type testToRun struct {
		testsharder.Test
		// The number of times the test has already been run.
		previousRuns int
		// The sum of the durations of all the test's previous runs.
		totalDuration time.Duration
	}

	// Since only a single goroutine writes to and reads from the queue it would
	// be more appropriate to use a true Queue data structure, but we'd need to
	// implement that ourselves so it's easier to just use a channel. Make the
	// channel double the necessary size just to be safe and avoid potential
	// deadlocks.
	testQueue := make(chan testToRun, 2*len(tests))
	for _, test := range tests {
		testQueue <- testToRun{Test: test}
	}

	// `for test := range testQueue` might seem simpler, but it would block
	// instead of exiting once the queue becomes empty. To exit the loop we
	// would need to close the channel when it became empty. That would require
	// a length check within the loop body anyway, and it's more robust to put
	// the length check in the for loop condition.
	for len(testQueue) > 0 {
		test := <-testQueue

		t, sinks, err := testerForTest(test.Test)
		if err != nil {
			return err
		}

		runIndex := test.previousRuns
		outDir := filepath.Join(globalOutDir, url.PathEscape(strings.ReplaceAll(test.Name, ":", "")), strconv.Itoa(runIndex))
		result, err := runTestOnce(ctx, test.Test, t, outDir)
		if err != nil {
			return err
		}
		result.RunIndex = runIndex
		if err := outputs.Record(*result); err != nil {
			return err
		}

		test.previousRuns++
		test.totalDuration += result.Duration()

		if shouldKeepGoing(test.Test, result, test.totalDuration) {
			// Schedule the test to be run again.
			testQueue <- test
		}
		// TODO(olivernewman): Add a unit test to make sure data sinks are
		// recorded correctly.
		*sinks = append(*sinks, result.DataSinks)
	}
	return nil
}

// shouldKeepGoing returns whether we should schedule another run of the test.
// It'll return true if we haven't yet exceeded the time limit for reruns, or
// if the most recent test run didn't meet the stop condition for this test.
func shouldKeepGoing(test testsharder.Test, lastResult *testrunner.TestResult, testTotalDuration time.Duration) bool {
	stopRepeatingDuration := time.Duration(test.StopRepeatingAfterSecs) * time.Second
	if stopRepeatingDuration > 0 && testTotalDuration >= stopRepeatingDuration {
		return false
	} else if test.Runs > 0 && lastResult.RunIndex+1 >= test.Runs {
		return false
	} else if test.RunAlgorithm == testsharder.StopOnSuccess && lastResult.Passed() {
		return false
	} else if test.RunAlgorithm == testsharder.StopOnFailure && !lastResult.Passed() {
		return false
	}
	return true
}

// runTestOnce runs the given test once. It will not return an error if the test
// fails, only if an unrecoverable error occurs or testing should otherwise stop.
func runTestOnce(
	ctx context.Context,
	test testsharder.Test,
	t testrunner.Tester,
	outDir string,
) (*testrunner.TestResult, error) {
	// The test case parser specifically uses stdout, so we need to have a
	// dedicated stdout buffer.
	stdout := new(bytes.Buffer)
	stdio := new(stdioBuffer)

	multistdout := io.MultiWriter(streams.Stdout(ctx), stdio, stdout)
	multistderr := io.MultiWriter(streams.Stderr(ctx), stdio)

	// In the case of running tests on QEMU over serial, we do not wish to
	// forward test output to stdout, as QEMU is already redirecting serial
	// output there: we do not want to double-print.
	//
	// This is a bit of a hack, but is a lesser evil than extending the
	// testrunner CLI just to sidecar the information of 'is QEMU'.
	againstQEMU := os.Getenv(botanistconstants.NodenameEnvKey) == target.DefaultQEMUNodename
	if _, ok := t.(*testrunner.FuchsiaSerialTester); ok && againstQEMU {
		multistdout = io.MultiWriter(stdio, stdout)
	}

	result := runtests.TestSuccess
	startTime := clock.Now(ctx)

	// Set the outer timeout to a slightly higher value in order to give the tester
	// time to handle the timeout itself.  Other steps such as retrying tests over
	// serial or fetching data sink references may also cause the Test() method to
	// exceed the test's timeout, so we give enough time for the tester to
	// complete those steps as well.
	outerTestTimeout := test.Timeout + testTimeoutGracePeriod

	var timeoutCh <-chan time.Time
	if test.Timeout > 0 {
		// Intentionally call After(), thereby resolving a completion deadline,
		// *before* starting to run the test. This helps avoid race conditions
		// in this function's unit tests that advance the fake clock's time
		// within the `t.Test()` call.
		timeoutCh = clock.After(ctx, outerTestTimeout)
	}
	// Else, timeoutCh will be nil. Receiving from a nil channel blocks forever,
	// so no timeout will be enforced, which is what we want.

	type testResult struct {
		dataSinks runtests.DataSinkReference
		err       error
	}
	ch := make(chan testResult, 1)

	// We don't use context.WithTimeout() because it uses the real time.Now()
	// instead of clock.Now(), which makes it much harder to simulate timeouts
	// in this function's unit tests.
	testCtx, cancelTest := context.WithCancel(ctx)
	defer cancelTest()
	// Run the test in a goroutine so that we don't block in case the tester fails
	// to respect the timeout.
	go func() {
		dataSinks, err := t.Test(testCtx, test, multistdout, multistderr, outDir)
		ch <- testResult{dataSinks, err}
	}()

	var dataSinks runtests.DataSinkReference
	var err error
	select {
	case res := <-ch:
		dataSinks = res.dataSinks
		err = res.err
	case <-timeoutCh:
		err = &testrunner.TimeoutError{outerTestTimeout}
		cancelTest()
	}
	if err != nil {
		if ctx.Err() != nil {
			// testrunner is shutting down, give up running tests and don't
			// report this test result as it may have been impacted by the
			// context cancelation.
			return nil, err
		}
		var errFatal testrunner.FatalError
		if errors.As(err, &errFatal) {
			// The tester encountered a fatal condition and cannot run any more
			// tests.
			return nil, err
		}
		result = runtests.TestFailure
		var timeoutErr *testrunner.TimeoutError
		if errors.As(err, &timeoutErr) {
			// TODO(fxbug.dev/49266): Emit a different "Timeout" result if the
			// test timed out.
			logger.Errorf(ctx, "Test %s timed out after %s", test.Name, timeoutErr.Timeout)
		} else {
			logger.Errorf(ctx, "Error running test %s: %s", test.Name, err)
		}
	}

	endTime := clock.Now(ctx)

	// Record the test details in the summary.
	return &testrunner.TestResult{
		Name:      test.Name,
		GNLabel:   test.Label,
		Stdio:     stdio.buf.Bytes(),
		Result:    result,
		Cases:     testparser.Parse(stdout.Bytes()),
		StartTime: startTime,
		EndTime:   endTime,
		DataSinks: dataSinks,
	}, nil
}
