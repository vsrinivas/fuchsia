// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"time"

	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testrunner"
	"fuchsia.googlesource.com/tools/testsharder"
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
)

// Fuchsia-specific environment variables possibly exposed to the testrunner.
const (
	nodenameEnvVar = "FUCHSIA_NODENAME"
	sshKeyEnvVar   = "FUCHSIA_SSH_KEY"
	testOutdirEnvVar = "FUCHSIA_TEST_OUTDIR"

	// An environment variable set by Swarming pointing to a particular directory from which
	// all files within will be isolated upon completion of a task.
	isolatedOutdirEnvVar = "ISOLATED_OUTDIR"
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
	flag.Usage = usage
}

func main() {
	flag.Parse()

	if help || flag.NArg() != 1 {
		flag.Usage()
		flag.PrintDefaults()
		return
	}

	// Load tests.
	testsPath := flag.Arg(0)
	tests, err := testsharder.LoadTests(testsPath)
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

func execute(tests []testsharder.Test, output *Output, nodename, sshKeyFile string) error {
	var linux, mac, fuchsia, unknown []testsharder.Test
	for _, test := range tests {
		switch test.OS {
		case testsharder.Fuchsia:
			fuchsia = append(fuchsia, test)
		case testsharder.Linux:
			linux = append(linux, test)
		case testsharder.Mac:
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
		env: append(
			os.Environ(),
			// We redirect this environment variable so that tests need not take a dependency on
			// LUCI infrastructure terms.
			fmt.Sprintf("%s=%s", testOutdirEnvVar, os.Getenv(isolatedOutdirEnvVar)),
		),
	}

	if err := runTests(linux, localTester.Test, output); err != nil {
		return err
	}

	if err := runTests(mac, localTester.Test, output); err != nil {
		return err
	}

	return runFuchsiaTests(fuchsia, output, nodename, sshKeyFile)
}

func runFuchsiaTests(tests []testsharder.Test, output *Output, nodename, sshKeyFile string) error {
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
	tester, err := NewFuchsiaTester(nodename, sshKey)
	if err != nil {
		return fmt.Errorf("failed to initialize fuchsia tester: %v", err)
	}
	defer tester.Close()
	return runTests(tests, tester.Test, output)
}

func runTests(tests []testsharder.Test, tester Tester, output *Output) error {
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

func runTest(ctx context.Context, test testsharder.Test, tester Tester) (*testrunner.TestResult, error) {
	result := runtests.TestSuccess
	stdout := new(bytes.Buffer)
	stderr := new(bytes.Buffer)

	// Fork the test's stdout and stderr streams to the test runner's stderr stream to
	// make local debugging easier.  Writing both to stderr ensures that stdout only
	// contains the test runner's TAP output stream.
	multistdout := io.MultiWriter(stdout, os.Stderr)
	multistderr := io.MultiWriter(stderr, os.Stderr)

	startTime := time.Now()

	if err := tester(ctx, test, multistdout, multistderr); err != nil {
		result = runtests.TestFailure
		log.Println(err)
	}

	endTime := time.Now()

	// Record the test details in the summary.
	return &testrunner.TestResult{
		Name:      test.Name,
		Stdout:    stdout.Bytes(),
		Stderr:    stderr.Bytes(),
		Result:    result,
		StartTime: startTime,
		EndTime:   endTime,
	}, nil
}
