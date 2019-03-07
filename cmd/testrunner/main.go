// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
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

// Fuchsia-specific environment variables possibly exposed to the testrunner.
const (
	nodenameEnvVar = "FUCHSIA_NODENAME"
	ipv4EnvVar     = "FUCHSIA_IPV4_ADDR"
	sshKeyEnvVar   = "FUCHSIA_SSH_KEY"
)

// FuchsiaDevice represents an ambient Fuchsia device for whose properties may be supplied in the environment.
type FuchsiaDevice struct {
	propertyNames []string
}

// InferFuchsiaDevice returns an ambient FuchsiaDevice if properties are set in the
// environment; else it returns nil.
func InferFuchsiaDevice() *FuchsiaDevice {
	device := FuchsiaDevice{
		propertyNames: []string{
			nodenameEnvVar,
			ipv4EnvVar,
			sshKeyEnvVar,
		},
	}
	if len(device.Environ()) == 0 {
		return nil
	}
	return &device
}

// SSHKey returns the private SSH key corresponding to the paved authorized key, if set.
func (device FuchsiaDevice) SSHKey() string {
	return os.Getenv(sshKeyEnvVar)
}

// Nodename returns the nodename of the device, if set.
func (device FuchsiaDevice) Nodename() string {
	return os.Getenv(nodenameEnvVar)
}

// Environ returns the full environment list of device properties set.
func (device FuchsiaDevice) Environ() []string {
	var environ []string
	for _, name := range device.propertyNames {
		if prop := os.Getenv(name); prop != "" {
			environ = append(environ, fmt.Sprintf("%s=%s", name, prop))
		}
	}
	return environ
}

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
	if err := execute(tests, output, InferFuchsiaDevice()); err != nil {
		log.Fatal(err)
	}
}

func execute(tests []testsharder.Test, output *Output, device *FuchsiaDevice) error {
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
	if device != nil {
		localTester.env = append(localTester.env, device.Environ()...)
	}

	if err := runTests(linux, localTester.Test, output); err != nil {
		return err
	}

	if err := runTests(mac, localTester.Test, output); err != nil {
		return err
	}

	return runFuchsiaTests(fuchsia, output, device)
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

func runFuchsiaTests(tests []testsharder.Test, output *Output, device *FuchsiaDevice) error {
	if len(tests) == 0 {
		return nil
	} else if device == nil {
		return errors.New("no fuchsia device present")
	} else if device.Nodename() == "" {
		return fmt.Errorf("%s must be set", nodenameEnvVar)
	} else if device.SSHKey() == "" {
		return fmt.Errorf("%s must be set", sshKeyEnvVar)
	}

	tester, err := NewFuchsiaTester(device.Nodename(), device.SSHKey())
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
