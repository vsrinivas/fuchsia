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

	// The test output directory to create on the Fuchsia device.
	fuchsiaOutputDir = "/data/infra/testrunner"

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
)

// TestRunnerOutput manages the output of this test runner.
type TestRunnerOutput struct {
	Summary *SummaryOutput
	TAP     *TAPOutput
	Tar     *TarOutput
}

func (o *TestRunnerOutput) Record(result testResult) {
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

type testResult struct {
	Name   string
	Output io.Reader
	Result runtests.TestResult
}

func usage() {
	fmt.Println(`testrunner [flags] tests-file

		Executes all tests found in the JSON [tests-file]
		Requires botanist.DeviceContext to have been registered and in the current
		environment; for more details see
		https://fuchsia.googlesource.com/tools/+/master/botanist/context.go.`)
}

func init() {
	flag.BoolVar(&help, "help", false, "Whether to show Usage and exit.")
	flag.StringVar(&archive, "archive", "", "Optional path where a tar archive containing test results should be created.")
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
		tar, err := NewTarOutput(archive)
		if err != nil {
			log.Fatalf("failed to initialize tar recorder: %v", err)
		}
		output.Tar = tar
	}

	// Prepare the Fuchsia DeviceContext.
	devCtx, err := botanist.GetDeviceContext()
	if err != nil {
		log.Fatal(err)
	}

	// Execute.
	if err := execute(tests, output, devCtx); err != nil {
		log.Fatal(err)
	}
}

func execute(tests []testsharder.Test, output *TestRunnerOutput, devCtx *botanist.DeviceContext) error {
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

	if err := runTests(linux, RunTestInSubprocess, output); err != nil {
		return err
	}

	if err := runTests(mac, RunTestInSubprocess, output); err != nil {
		return err
	}

	return runFuchsiaTests(fuchsia, output, devCtx)
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

func runFuchsiaTests(tests []testsharder.Test, output *TestRunnerOutput, devCtx *botanist.DeviceContext) error {
	if len(tests) == 0 {
		return nil
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

	// Record log_listener output. This goes into the output archive as "syslog.txt"
	session, err := sshClient.NewSession()
	if err != nil {
		return err
	}
	defer session.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	syslogStdout := new(bytes.Buffer)
	syslogStderr := new(bytes.Buffer)
	runner := &testrunner.SSHRunner{Session: session}
	go runner.Run(ctx, []string{"bin/log_listener"}, syslogStdout, syslogStderr)

	// Ensure the syslog is always included in the output, even if tests fail.
	// TODO(IN-824): Run fuchsia/linux/mac tests in go-routines and emit errors on channels.
	defer func() {
		if output.Tar == nil {
			return
		}

		if err := output.Tar.TarFile(syslogStdout.Bytes(), syslogStdoutFilename); err != nil {
			log.Println(err)
		}

		if err := output.Tar.TarFile(syslogStderr.Bytes(), syslogStderrFilename); err != nil {
			log.Println(err)
		}
	}()

	return runTests(tests, fuchsiaTester.Test, output)
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

func runTest(ctx context.Context, test testsharder.Test, tester Tester) (*testResult, error) {
	result := runtests.TestSuccess
	output := new(bytes.Buffer)
	multistdout := io.MultiWriter(output, os.Stdout)
	multistderr := io.MultiWriter(output, os.Stderr)
	if err := tester(ctx, test, multistdout, multistderr); err != nil {
		result = runtests.TestFailure
		log.Println(err)
	}
	// Record the test details in the summary.
	return &testResult{
		Name:   test.Name,
		Output: output,
		Result: result,
	}, nil
}
