// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"runtime"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/tap/lib"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

// Fuchsia-specific environment variables possibly exposed to the testrunner.
const (
	nodenameEnvVar = "FUCHSIA_NODENAME"
	sshKeyEnvVar   = "FUCHSIA_SSH_KEY"
	// A directory that will be automatically archived on completion of a task.
	testOutdirEnvVar = "FUCHSIA_TEST_OUTDIR"
)

// Command-line flags
var (
	// Whether to show Usage and exit.
	help bool

	// The path where a tar archive containing test results should be created.
	archive string

	// Working directory of the local testing subprocesses.
	localWD string

	// Whether to use runtests when executing tests on fuchsia. If false, the
	// default will be run_test_component.
	useRuntests bool

	// Per-test timeout.
	perTestTimeout time.Duration
)

func usage() {
	fmt.Printf(`testrunner [flags] tests-file

Executes all tests found in the JSON [tests-file]
Fuchsia tests require both the nodename of the fuchsia instance and a private
SSH key corresponding to a authorized key to be set in the environment under
%s and %s respectively.
`, nodenameEnvVar, sshKeyEnvVar)
}

func init() {
	flag.BoolVar(&help, "help", false, "Whether to show Usage and exit.")
	flag.StringVar(&archive, "archive", "", "Optional path where a tar archive containing test results should be created.")
	flag.StringVar(&localWD, "C", "", "Working directory of local testing subprocesses; if unset the current working directory will be used.")
	flag.BoolVar(&useRuntests, "use-runtests", false, "Whether to default to running fuchsia tests with runtests; if false, run_test_component will be used.")
	// TODO(fxb/36480): Support different timeouts for different tests.
	flag.DurationVar(&perTestTimeout, "per-test-timeout", 0, "Per-test timeout, applied to all tests. Ignored if <= 0.")
	flag.Usage = usage
}

func main() {
	flag.Parse()

	if help || flag.NArg() != 1 {
		flag.Usage()
		flag.PrintDefaults()
		return
	}

	l := logger.NewLogger(logger.DebugLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "testrunner ")
	ctx := logger.WithLogger(context.Background(), l)

	testsPath := flag.Arg(0)
	tests, err := loadTests(testsPath)
	if err != nil {
		log.Fatalf("failed to load tests from %q: %v", testsPath, err)
	}

	// Configure a test outputs object, responsible for producing TAP output,
	// recording data sinks, and archiving other test outputs.
	testOutdir := os.Getenv(testOutdirEnvVar)
	if testOutdir == "" {
		var err error
		testOutdir, err = ioutil.TempDir("", "testrunner")
		if err != nil {
			log.Fatalf("failed to create a test output directory")
		}
	}
	logger.Debugf(ctx, "test output directory: %s", testOutdir)

	tapProducer := tap.NewProducer(os.Stdout)
	tapProducer.Plan(len(tests))
	outputs, err := createTestOutputs(tapProducer, testOutdir, archive)
	if err != nil {
		log.Fatalf("failed to create test results object: %v", err)
	}
	defer outputs.Close()

	nodename := os.Getenv(nodenameEnvVar)
	sshKeyFile := os.Getenv(sshKeyEnvVar)

	if err := execute(ctx, tests, outputs, nodename, sshKeyFile); err != nil {
		log.Fatal(err)
	}
}

func loadTests(path string) ([]build.Test, error) {
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %v", path, err)
	}

	var tests []build.Test
	if err := json.Unmarshal(bytes, &tests); err != nil {
		return nil, fmt.Errorf("failed to unmarshal %q: %v", path, err)
	}

	return tests, nil
}

type tester interface {
	Test(context.Context, build.Test, io.Writer, io.Writer) (runtests.DataSinkMap, error)
	Close() error
}

func execute(ctx context.Context, tests []build.Test, outputs *testOutputs, nodename, sshKeyFile string) error {
	var localTests, fuchsiaTests []build.Test
	for _, test := range tests {
		switch test.OS {
		case "fuchsia":
			fuchsiaTests = append(fuchsiaTests, test)
		case "linux":
			if runtime.GOOS != "linux" {
				return fmt.Errorf("cannot run linux tests when GOOS = %q", runtime.GOOS)
			}
			localTests = append(localTests, test)
		case "mac":
			if runtime.GOOS != "darwin" {
				return fmt.Errorf("cannot run mac tests when GOOS = %q", runtime.GOOS)
			}
			localTests = append(localTests, test)
		default:
			return fmt.Errorf("test %#v has unsupported OS: %q", test, test.OS)
		}
	}

	localTester := newSubprocessTester(localWD, os.Environ())
	if err := runTests(ctx, localTests, localTester, outputs); err != nil {
		return err
	}

	if len(fuchsiaTests) == 0 {
		return nil
	}

	var t tester
	var err error
	if sshKeyFile != "" {
		if nodename == "" {
			return fmt.Errorf("%s must be set", nodenameEnvVar)
		}
		t, err = newFuchsiaSSHTester(nodename, sshKeyFile, outputs.dataDir, useRuntests)
	} else {
		// TODO(fxbug.dev/41930): create a serial test runner in this case.
		return fmt.Errorf("%s must be set", sshKeyEnvVar)
	}
	if err != nil {
		return fmt.Errorf("failed to initialize fuchsia tester: %v", err)
	}
	defer t.Close()

	return runTests(ctx, fuchsiaTests, t, outputs)
}

func runTests(ctx context.Context, tests []build.Test, t tester, outputs *testOutputs) error {
	for _, test := range tests {
		result := runTest(ctx, test, t)
		if err := outputs.record(*result); err != nil {
			return err
		}
	}
	return nil
}

func runTest(ctx context.Context, test build.Test, t tester) *testrunner.TestResult {
	result := runtests.TestSuccess
	stdout := new(bytes.Buffer)
	stderr := new(bytes.Buffer)

	multistdout := io.MultiWriter(stdout, os.Stdout)
	multistderr := io.MultiWriter(stderr, os.Stderr)

	if perTestTimeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, perTestTimeout)
		defer cancel()
	}

	startTime := time.Now()
	dataSinks, err := t.Test(ctx, test, multistdout, multistderr)
	if err != nil {
		result = runtests.TestFailure
		if err == context.DeadlineExceeded {
			logger.Errorf(ctx, "test killed because timeout reached (%v)", perTestTimeout)
		} else {
			logger.Errorf(ctx, err.Error())
		}
	}

	endTime := time.Now()

	// test.Name is available but is not necessarily a unique identifier for
	// each test, so we use either path or package URL instead.
	name := test.Path
	if test.OS == "fuchsia" {
		name = test.PackageURL
	}

	// Record the test details in the summary.
	return &testrunner.TestResult{
		Name:      name,
		GNLabel:   test.Label,
		Stdout:    stdout.Bytes(),
		Stderr:    stderr.Bytes(),
		Result:    result,
		StartTime: startTime,
		EndTime:   endTime,
		DataSinks: dataSinks,
	}
}
