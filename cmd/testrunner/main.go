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
	"os"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/runtests"
	"fuchsia.googlesource.com/tools/testrunner"
	"fuchsia.googlesource.com/tools/testsharder"
	"golang.org/x/crypto/ssh"
)

const (
	// Default amount of time to wait before failing to perform any IO action.
	defaultIOTimeout = 1 * time.Minute

	// The username used to authenticate with the Fuchsia device.
	sshUser = "fuchsia"

	// The filename to use for log_listener's stdout captured during Fuchsia tests.
	syslogStdoutFilename = "syslog-stdout.txt"

	// The filename to use for log_listener's stderr captured during Fuchsia tests.
	syslogStderrFilename = "syslog-stderr.txt"
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

// TestRunnerOutput manages the output of this test runner.
type TestRunnerOutput struct {
	Summary *SummaryOutput
	TAP     *TAPOutput
	Tar     *TarOutput
}

func (o *TestRunnerOutput) Record(result testrunner.TestResult) {
	if o.Summary != nil {
		o.Summary.Record(result)
	}

	if o.TAP != nil {
		o.TAP.Record(result)
	}

	if o.Tar != nil {
		o.Tar.Record(result)
	}
}

// Complete finishes producing output for the test run.
func (o *TestRunnerOutput) Complete() error {
	if o.Tar == nil {
		return nil
	}

	bytes, err := json.Marshal(o.Summary.Summary)
	if err != nil {
		return err
	}

	if err := o.Tar.TarFile(bytes, runtests.TestSummaryFilename); err != nil {
		return err
	}

	return o.Tar.Close()
}

func usage() {
	fmt.Println(`testrunner [flags] tests-file

Executes all tests found in the JSON [tests-file]
Fuchsia tests require both the nodename of the fuchsia instance and a private
SSH key corresponding to a authorized key to be set in the environment under
NODENAME and SSH_KEY respectively.`)
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
	tests, err := testrunner.LoadTests(testsPath)
	if err != nil {
		log.Fatalf("failed to load tests from %q: %v", testsPath, err)
	}

	// Prepare test output drivers.
	output := &TestRunnerOutput{
		TAP:     NewTAPOutput(os.Stdout, len(tests)),
		Summary: &SummaryOutput{},
	}
	defer output.Complete()

	// Add an archive Output if specified.
	if archive != "" {
		output.Tar, err = NewTarOutput(archive)
		if err != nil {
			log.Fatalf("failed to initialize tar recorder: %v", err)
		}
	}

	nodename := os.Getenv("NODENAME")
	if nodename == "" {
		log.Printf("NODENAME not set")
	}
	sshKey := os.Getenv("SSH_KEY")
	if sshKey == "" {
		log.Printf("SSH_KEY not set")
	}

	// Execute.
	if err := execute(tests, output, nodename, sshKey); err != nil {
		log.Fatal(err)
	}
}

func execute(tests []testsharder.Test, output *TestRunnerOutput, nodename, sshKey string) error {
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
		wd: localWD,
	}
	if nodename != "" {
		localTester.env = append(
			localTester.env,
			fmt.Sprintf("NODENAME=%s", nodename),
		)
	}
	if sshKey != "" {
		localTester.env = append(
			localTester.env,
			fmt.Sprintf("SSH_KEY=%s", sshKey),
		)
	}


	if err := runTests(linux, localTester.Test, output); err != nil {
		return err
	}

	if err := runTests(mac, localTester.Test, output); err != nil {
		return err
	}

	return runFuchsiaTests(fuchsia, output, nodename, sshKey)
}

func sshIntoNode(nodename, privateKey string) (*ssh.Client, error) {
	signer, err := ssh.ParsePrivateKey([]byte(privateKey))
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

func runFuchsiaTests(tests []testsharder.Test, output *TestRunnerOutput, nodename, sshKey string) error {
	if len(tests) == 0 {
		return nil
	}

	if nodename == "" {
		return errors.New("NODENAME must be set")
	} else if sshKey == "" {
		return errors.New("SSH_KEY must be set")
	}

	tester, err := NewFuchsiaTester(nodename, sshKey)
	if err != nil {
		return fmt.Errorf("failed to initialize fuchsia tester: %v", err)
	}
	defer tester.Close()

	// Ensure the syslog is always included in the output, even if tests fail.
	// TODO(IN-824): Run fuchsia/linux/mac tests in go-routines and emit errors on channels.
	defer func() {
		if output.Tar == nil {
			return
		}

		stdout, stderr := tester.SyslogOutput()
		stdoutBytes, err := ioutil.ReadAll(stdout)
		if err != nil {
			log.Println(err)
			return
		}
		stderrBytes, err := ioutil.ReadAll(stderr)
		if err != nil {
			log.Println(err)
			return
		}

		if err := output.Tar.TarFile(stdoutBytes, syslogStdoutFilename); err != nil {
			log.Println(err)
		}
		output.Summary.AddFile("syslog-stdout", syslogStdoutFilename)

		if err := output.Tar.TarFile(stderrBytes, syslogStderrFilename); err != nil {
			log.Println(err)
		}
		output.Summary.AddFile("syslog-stderr", syslogStderrFilename)
	}()

	return runTests(tests, tester.Test, output)
}

func runTests(tests []testsharder.Test, tester Tester, output *TestRunnerOutput) error {
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
