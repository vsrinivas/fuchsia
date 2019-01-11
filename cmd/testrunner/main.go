// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path"
	"strings"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testsharder"
	"golang.org/x/crypto/ssh"
)

// TODO(IN-824): Produce a tar archive of all output files.
// TODO(IN-824): Include log_listener output.

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

func usage() {
	fmt.Println(`
		testrunner [flags] tests-file

		Executes all tests found in the JSON [tests-file]
		Required environment variables:

		"NODENAME":  The nodename of the attached Fuchsia device to use for testing. This
		           can usually be found using the 'netls' tool.
		"SSH_KEY":   Path to the SSH private key used to connect to the Fuchsia device.
	`)
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

	if err := execute(flag.Arg(0)); err != nil {
		log.Fatal(err)
	}
}

func execute(testsFilepath string) error {
	// Validate inputs.
	nodename := os.Getenv("NODENAME")
	if nodename == "" {
		return errors.New("missing environment variable NODENAME")
	}

	privateKeyPath := os.Getenv("SSH_KEY")
	if privateKeyPath == "" {
		return errors.New("missing environment variable SSH_KEY")
	}

	// Initialize the connection to the Fuchsia device.
	sshClient, err := sshIntoNode(nodename, privateKeyPath)
	if err != nil {
		return fmt.Errorf("failed to connect to node %q: %v", nodename, err)
	}

	fuchsiaTester := &FuchsiaTester{
		remoteOutputDir: fuchsiaOutputDir,
		delegate: &SSHTester{
			client: sshClient,
		},
	}

	// Parse test input.
	bytes, err := ioutil.ReadFile(testsFilepath)
	if err != nil {
		return fmt.Errorf("failed to read %s: %v", testsFilepath, err)
	}

	var tests []testsharder.Test
	if err := json.Unmarshal(bytes, &tests); err != nil {
		return fmt.Errorf("failed to unmarshal %s: %v", testsFilepath, err)
	}

	// Execute all tests.
	summary, err := runTests(tests, fuchsiaTester.Test, RunTestInSubprocess, outputDir)
	if err != nil {
		return err
	}

	summaryFile, err := os.Create(path.Join(outputDir, "summary.json"))
	if err != nil {
		return err
	}

	// Log summary to `outputDir`.
	encoder := json.NewEncoder(summaryFile)
	return encoder.Encode(summary)
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

func runTests(tests []testsharder.Test, fuchsia Tester, local Tester, outputDir string) (*runtests.TestSummary, error) {
	// Execute all tests.
	summary := new(runtests.TestSummary)
	for _, test := range tests {
		var tester Tester
		switch strings.ToLower(test.OS) {
		case "fuchsia":
			tester = fuchsia
		case "linux", "mac":
			tester = local
		default:
			log.Printf("cannot run '%s' on unknown OS '%s'", test.Name, test.OS)
			continue
		}

		details, err := runTest(context.Background(), test, tester, outputDir)
		if err != nil {
			log.Println(err)
		}

		if details != nil {
			summary.Tests = append(summary.Tests, *details)
		}
	}

	return summary, nil
}

func runTest(ctx context.Context, test testsharder.Test, tester Tester, outputDir string) (*runtests.TestDetails, error) {
	// Create a file for test output.
	output, err := os.Create(path.Join(outputDir, test.Name, runtests.TestOutputFilename))
	if err != nil {
		return nil, err
	}
	defer output.Close()

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
