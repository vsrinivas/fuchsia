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
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/lib"
)

const (
	// Default amount of time to wait before failing to perform any IO action.
	defaultIOTimeout = 1 * time.Minute

	// The username used to authenticate with the Fuchsia device.
	sshUser = "fuchsia"
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

// Fuchsia-specific environment variables possibly exposed to the testrunner.
const (
	nodenameEnvVar = "FUCHSIA_NODENAME"
	sshKeyEnvVar   = "FUCHSIA_SSH_KEY"
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

	// Have logs output timestamps with milliseconds.
	log.SetFlags(log.Ldate | log.Lmicroseconds)

	// Load tests.
	testsPath := flag.Arg(0)
	tests, err := loadTests(testsPath)
	if err != nil {
		log.Fatalf("failed to load tests from %q: %v", testsPath, err)
	}

	// Prepare test output drivers.
	output := new(Output)
	defer output.Complete()
	output.SetupTAP(os.Stdout, len(tests))
	output.SetupSummary()
	if archive != "" {
		if err := output.SetupTar(archive); err != nil {
			log.Fatalf("failed to initialize tar output: %v", err)
		}
	}

	// Execute.
	nodename := os.Getenv(nodenameEnvVar)
	sshKeyFile := os.Getenv(sshKeyEnvVar)
	if err := execute(tests, output, nodename, sshKeyFile); err != nil {
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

func execute(tests []build.Test, output *Output, nodename, sshKeyFile string) error {
	var linux, mac, fuchsia, unknown []build.Test
	for _, test := range tests {
		switch test.OS {
		case "fuchsia":
			fuchsia = append(fuchsia, test)
		case "linux":
			linux = append(linux, test)
		case "mac":
			mac = append(mac, test)
		default:
			unknown = append(unknown, test)
		}
	}

	if len(unknown) > 0 {
		return fmt.Errorf("could not determine the runtime system for following tests %v", unknown)
	}

	localTester := &SubprocessTester{
		dir: localWD,
		env: os.Environ(),
	}

	if err := runTests(linux, localTester.Test, output); err != nil {
		return err
	}

	if err := runTests(mac, localTester.Test, output); err != nil {
		return err
	}

	return runFuchsiaTests(fuchsia, output, nodename, sshKeyFile)
}

func runFuchsiaTests(tests []build.Test, output *Output, nodename, sshKeyFile string) error {
	if len(tests) == 0 {
		return nil
	} else if nodename == "" {
		return fmt.Errorf("%s must be set", nodenameEnvVar)
	} else if sshKeyFile == "" {
		return fmt.Errorf("%s must be set", sshKeyEnvVar)
	}

	sshKey, err := ioutil.ReadFile(sshKeyFile)
	if err != nil {
		return err
	}
	tester, err := NewFuchsiaTester(nodename, sshKey, useRuntests)
	if err != nil {
		return fmt.Errorf("failed to initialize fuchsia tester: %v", err)
	}
	defer tester.Close()
	return runTests(tests, tester.Test, output)
}

func runTests(tests []build.Test, tester Tester, output *Output) error {
	for _, test := range tests {
		result, err := runTest(context.Background(), test, tester)
		if err != nil {
			log.Println(err)
		}
		if result != nil {
			output.Record(*result)
		}
	}
	return nil
}

func runTest(ctx context.Context, test build.Test, tester Tester) (*testrunner.TestResult, error) {
	result := runtests.TestSuccess
	stdout := new(bytes.Buffer)
	stderr := new(bytes.Buffer)

	// Fork the test's stdout and stderr streams to the test runner's stderr stream to
	// make local debugging easier.  Writing both to stderr ensures that stdout only
	// contains the test runner's TAP output stream.
	multistdout := io.MultiWriter(stdout, os.Stderr)
	multistderr := io.MultiWriter(stderr, os.Stderr)

	if perTestTimeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, perTestTimeout)
		defer cancel()
	}

	startTime := time.Now()

	if err := tester(ctx, test, multistdout, multistderr); err != nil {
		result = runtests.TestFailure
		if err == context.DeadlineExceeded {
			log.Printf("Test killed because timeout reached (%v)", perTestTimeout)
		} else {
			log.Println(err)
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
	}, nil
}
