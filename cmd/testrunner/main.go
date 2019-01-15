// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path"
	"sort"
	"strings"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/tap"
	"fuchsia.googlesource.com/tools/testrunner"
	"fuchsia.googlesource.com/tools/testsharder"
	"golang.org/x/crypto/ssh"
)

// TODO(IN-824): Produce a tar archive of all output files.

const (
	// Default amount of time to wait before failing to perform any IO action.
	defaultIOTimeout = 1 * time.Minute

	// The username used to authenticate with the Fuchsia device.
	sshUser = "fuchsia"

	// The test output directory to create on the Fuchsia device.
	fuchsiaOutputDir = "/data/infra/testrunner"
)

// Command-line flags
var (
	// Whether to show Usage and exit.
	help bool

	// The directory where output should be written. This path will contain both
	// summary.json and the set of output files for each test.
	outputDir string

	// The path to a file containing properties of the Fuchsia device to use for testing.
	deviceFilepath string
)

// TestRecorder records the details of test run.
type TestRecorder func(runtests.TestDetails)

func usage() {
	fmt.Println(`testrunner [flags] tests-file

		Executes all tests found in the JSON [tests-file]
		Requires botanist.DeviceContext to have been registered and in the current
		environment; for more details see
		https://fuchsia.googlesource.com/tools/+/master/botanist/context.go.`)
}

func init() {
	flag.BoolVar(&help, "help", false, "Whether to show Usage and exit.")
	flag.StringVar(&outputDir, "output", "", "Directory where output should be written")
	flag.Usage = usage
}

func main() {
	flag.Parse()

	if help || flag.NArg() != 1 {
		flag.Usage()
		flag.PrintDefaults()
		return
	}

	if outputDir == "" {
		log.Fatal("-output is required")
	}

	// Load tests.
	testsPath := flag.Arg(0)
	tests, err := testrunner.LoadTests(testsPath)
	if err != nil {
		log.Fatalf("failed to load tests from %q: %v", testsPath, err)
	}

	// Prepare outputs.
	tapp := tap.NewProducer(os.Stdout)
	tapp.Plan(len(tests))
	var summary runtests.TestSummary
	recordDetails := func(details runtests.TestDetails) {
		tapp.Ok(details.Result == runtests.TestSuccess, details.Name)
		summary.Tests = append(summary.Tests, details)
	}

	// Execute.
	if err := execute(tests, recordDetails); err != nil {
		log.Fatal(err)
	}

	// Write Summary.
	file, err := os.Create(path.Join(outputDir, "summary.json"))
	if err != nil {
		log.Fatal(err)
	}

	encoder := json.NewEncoder(file)
	if err := encoder.Encode(summary); err != nil {
		log.Fatal(err)
	}
}

func execute(tests []testsharder.Test, recorder TestRecorder) error {
	// Partition the tests into groups according to OS.
	groups := groupTests(tests, func(test testsharder.Test) string {
		sys := strings.ToLower(test.OS)
		switch sys {
		case "fuchsia", "linux", "mac":
			return sys
		}
		return "unknown"
	})

	// Fail fast if any test cannot be run.
	if unknownTests, ok := groups["unknown"]; ok {
		return fmt.Errorf("could not determine the runtime system for following tests %v", unknownTests)
	}

	// Execute UNIX tests locally, assuming we're running in a UNIX environment.
	localTests := append(groups["linux"], groups["mac"]...)
	if len(localTests) > 0 {
		if err := runTests(localTests, RunTestInSubprocess, outputDir, recorder); err != nil {
			return err
		}
	}

	// Execute Fuchsia tests.
	return runFuchsiaTests(groups["fuchsia"], outputDir, recorder)
}

// groupTests splits a list of tests into named subgroups according to the names returned
// by `name`.  Within any subgroup, the list of tests is sorted by test name.
func groupTests(input []testsharder.Test, name func(testsharder.Test) string) map[string][]testsharder.Test {
	tests := make([]testsharder.Test, len(input))
	copy(tests, input)

	sort.SliceStable(tests, func(i, j int) bool {
		return tests[i].Name < tests[j].Name
	})

	output := make(map[string][]testsharder.Test)
	for _, test := range tests {
		group := name(test)
		output[group] = append(output[group], test)
	}

	return output
}

func sshIntoNode(nodename, privateKeyPath string) (*ssh.Client, error) {
	privateKey, err := ioutil.ReadFile(privateKeyPath)
	if err != nil {
		return nil, err
	}

	signer, err := ssh.ParsePrivateKey(privateKey)
	if err != nil {
		return nil, err
	}

	config := &ssh.ClientConfig{
		User: sshUser,
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(signer),
		},
		Timeout:         defaultIOTimeout,
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}

	return botanist.SSHIntoNode(context.Background(), nodename, config)
}

func runFuchsiaTests(tests []testsharder.Test, outputDir string, record TestRecorder) error {
	if len(tests) == 0 {
		return nil
	}

	devCtx, err := botanist.GetDeviceContext()
	if err != nil {
		return err
	}

	// Initialize the connection to the Fuchsia device.
	sshClient, err := sshIntoNode(devCtx.Nodename, devCtx.SSHKey)
	if err != nil {
		return fmt.Errorf("failed to connect to node %q: %v", devCtx.Nodename, err)
	}
	defer sshClient.Close()

	fuchsiaTester := &FuchsiaTester{
		remoteOutputDir: fuchsiaOutputDir,
		delegate: &SSHTester{
			client: sshClient,
		},
	}

	return runTests(tests, fuchsiaTester.Test, outputDir, record)
}

func runTests(tests []testsharder.Test, tester Tester, outputDir string, record TestRecorder) error {
	for _, test := range tests {
		details, err := runTest(context.Background(), test, tester, outputDir)
		if err != nil {
			log.Println(err)
		}

		if details != nil {
			record(*details)
		}
	}

	return nil
}

func runTest(ctx context.Context, test testsharder.Test, tester Tester, outputDir string) (*runtests.TestDetails, error) {
	// Prepare an output file for the test.
	workspace := path.Join(outputDir, test.Name)
	if err := os.MkdirAll(workspace, os.FileMode(0755)); err != nil {
		return nil, err
	}

	output, err := os.Create(path.Join(workspace, runtests.TestOutputFilename))
	if err != nil {
		return nil, err
	}
	defer output.Close()

	// Execute the test.
	result := runtests.TestSuccess
	multistdout := io.MultiWriter(output, os.Stdout)
	multistderr := io.MultiWriter(output, os.Stderr)
	if err := tester(ctx, test, multistdout, multistderr); err != nil {
		result = runtests.TestFailure
		log.Println(err)
	}

	// Record the test details in the summary.
	return &runtests.TestDetails{
		Name:       test.Name,
		OutputFile: output.Name(),
		Result:     result,
	}, nil
}
