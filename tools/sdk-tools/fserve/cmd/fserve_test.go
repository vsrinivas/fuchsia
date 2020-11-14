// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"syscall"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

// test argument pointing to the directory containing the testdata directory.
// This is configured in the BUILD.gn file.
var testrootFlag = flag.String("testroot", "", "Root directory of the files needed to execute the test.")

// Context with a logger used to test.
func testingContext() context.Context {
	flags := log.Ltime | log.Lshortfile
	log := logger.NewLogger(logger.DebugLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "fserve_test ")
	log.SetFlags(flags)
	return logger.WithLogger(context.Background(), log)
}

// See exec_test.go for details, but effectively this runs the function called TestHelperProcess passing
// the args.
func helperCommandForFServe(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFServe", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

func TestKillServers(t *testing.T) {
	ctx := testingContext()
	ExecCommand = helperCommandForFServe
	findProcess = mockedFindProcess
	defer func() {
		ExecCommand = exec.Command
		findProcess = defaultFindProcess
	}()

	// Test no existing servers
	os.Setenv("FSERVE_TEST_NO_SERVERS", "1")
	if err := killServers(ctx, ""); err != nil {
		t.Fatal(err)
	}
	// Test existing servers
	os.Setenv("FSERVE_TEST_NO_SERVERS", "0")
	if err := killServers(ctx, ""); err != nil {
		t.Fatal(err)
	}
	if err := killServers(ctx, "8083"); err != nil {
		t.Fatal(err)
	}
	os.Setenv("FSERVE_TEST_PGREP_ERROR", "1")
	err := killServers(ctx, "")
	if err == nil {
		t.Fatal("Expected error running pgrep, got no error.")
	}
	expected := "Error running pgrep: Expected error\n"
	actual := fmt.Sprintf("%v", err)
	if expected != actual {
		t.Fatalf("[%v], got [%v]", expected, actual)
	}

	os.Setenv("FSERVE_TEST_PGREP_ERROR", "0")
	os.Setenv("FSERVE_TEST_PS_ERROR", "1")
	err = killServers(ctx, "")
	if err == nil {
		t.Fatal("Expected error running ps, got no error.")
	}
	expected = "Error running ps: Expected error\n"
	actual = fmt.Sprintf("%v", err)
	if expected != actual {
		t.Fatalf("[%v], got [%v]", expected, actual)
	}
}

func TestStartServer(t *testing.T) {
	testSDK := sdkcommon.SDKProperties{
		DataPath: "/fake",
	}
	repoPath := "/fake/repo/path"
	repoPort := "8083"
	ExecCommand = helperCommandForFServe
	syscallWait4 = mockWait4NoError
	defer func() {
		ExecCommand = exec.Command
		syscallWait4 = defaultsyscallWait4
	}()

	if _, err := startServer(testSDK, repoPath, repoPort); err != nil {
		t.Fatal(err)
	}
	syscallWait4 = mockWait4WithError
	if _, err := startServer(testSDK, repoPath, repoPort); err != nil {
		expected := "Server started then exited with code 1"
		actual := fmt.Sprintf("%v", err)
		if expected != actual {
			t.Fatalf("[%v], got [%v]", expected, actual)
		}
	} else {
		t.Fatal("Expected error starting server, got no error.")
	}
}

func TestDownloadImageIfNeeded(t *testing.T) {
	testSDK := sdkcommon.SDKProperties{
		DataPath: t.TempDir(),
	}
	ctx := testingContext()
	ExecCommand = helperCommandForFServe
	sdkcommon.ExecCommand = helperCommandForFServe
	sdkcommon.ExecLookPath = func(cmd string) (string, error) { return filepath.Join("mocked", cmd), nil }
	defer func() {
		ExecCommand = exec.Command
		sdkcommon.ExecCommand = exec.Command
		sdkcommon.ExecLookPath = exec.LookPath
	}()
	version := "any-version"
	bucket := "test-bucket"
	srcPath := "gs://test-bucket/path/on/GCS/theImage.tgz"
	imageFilename := "theImage.tgz"

	executable, _ := os.Executable()
	fmt.Fprintf(os.Stderr, "Running test executable %v\n", executable)
	fmt.Fprintf(os.Stderr, "testrootFlag value   is %v\n", *testrootFlag)
	testrootPath := filepath.Join(filepath.Dir(executable), *testrootFlag)
	os.Setenv("FSERVE_TEST_TESTROOT", testrootPath)

	if err := downloadImageIfNeeded(ctx, testSDK, version, bucket, srcPath, imageFilename); err != nil {
		t.Fatal(err)
	}
	// Run the test again, and it should skip the download
	os.Setenv("FSERVE_TEST_ASSERT_NO_DOWNLOAD", "1")
	if err := downloadImageIfNeeded(ctx, testSDK, version, bucket, srcPath, imageFilename); err != nil {
		t.Fatal(err)
	}
}

func TestDownloadImageIfNeededCopiedFails(t *testing.T) {
	testSDK := sdkcommon.SDKProperties{
		DataPath: t.TempDir(),
	}
	ctx := testingContext()
	ExecCommand = helperCommandForFServe
	sdkcommon.ExecCommand = helperCommandForFServe
	sdkcommon.ExecLookPath = func(cmd string) (string, error) { return filepath.Join("mocked", cmd), nil }
	defer func() {
		ExecCommand = exec.Command
		sdkcommon.ExecCommand = exec.Command
		sdkcommon.ExecLookPath = exec.LookPath
	}()
	version := "any-version"
	bucket := "test-bucket"
	srcPath := "gs://test-bucket/path/on/GCS/theImage.tgz"
	imageFilename := "theImage.tgz"

	// Run the test again, and it should skip the download
	os.Setenv("FSERVE_TEST_ASSERT_NO_DOWNLOAD", "")
	os.Setenv("FSERVE_TEST_COPY_FAILS", "1")
	if err := downloadImageIfNeeded(ctx, testSDK, version, bucket, srcPath, imageFilename); err != nil {
		destPath := filepath.Join(testSDK.DataPath, imageFilename)
		expected := fmt.Sprintf("Could not copy image from %v to %v: BucketNotFoundException: 404 %v bucket does not exist.: exit status 2",
			srcPath, destPath, srcPath)
		actual := fmt.Sprintf("%v", err)
		if expected != actual {
			t.Fatalf("[%v], got [%v]", expected, actual)
		}
	} else {
		t.Fatal("Expected error downloading, got no error.")
	}
}

const resolvedAddr = "fe80::c0ff:eee:fe00:4444%en0"

func TestSetPackageSource(t *testing.T) {
	testSDK := sdkcommon.SDKProperties{
		DataPath: t.TempDir(),
	}
	homeDir := filepath.Join(testSDK.DataPath, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0755); err != nil {
		t.Fatal(err)
	}
	ctx := testingContext()
	ExecCommand = helperCommandForFServe
	sdkcommon.ExecCommand = helperCommandForFServe
	sdkcommon.GetUserHomeDir = func() (string, error) { return homeDir, nil }
	sdkcommon.GetUsername = func() (string, error) { return "testuser", nil }
	sdkcommon.GetHostname = func() (string, error) { return "testhost", nil }
	defer func() {
		ExecCommand = exec.Command
		sdkcommon.ExecCommand = exec.Command
		sdkcommon.GetUserHomeDir = sdkcommon.DefaultGetUserHomeDir
		sdkcommon.GetUsername = sdkcommon.DefaultGetUsername
		sdkcommon.GetHostname = sdkcommon.DefaultGetHostname
	}()
	repoPort := "8083"
	deviceName := ""
	deviceIP := resolvedAddr
	sshConfig := ""
	privateKey := ""
	name := "devhost"

	if err := setPackageSource(ctx, testSDK, repoPort, name, deviceName, deviceIP, sshConfig, privateKey); err != nil {
		t.Fatal(err)
	}
	deviceIP = "10.10.0.1"
	if err := setPackageSource(ctx, testSDK, repoPort, name, deviceName, deviceIP, sshConfig, privateKey); err != nil {
		t.Fatal(err)
	}

	deviceIP = ""
	deviceName = "test-device"
	if err := setPackageSource(ctx, testSDK, repoPort, name, deviceName, deviceIP, sshConfig, privateKey); err != nil {
		t.Fatal(err)
	}

	deviceIP = ""
	deviceName = "test-device"
	sshConfig = "custom-sshconfig"
	os.Setenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG", "1")
	if err := setPackageSource(ctx, testSDK, repoPort, name, deviceName, deviceIP, sshConfig, privateKey); err != nil {
		t.Fatal(err)
	}

	deviceIP = ""
	deviceName = "test-device"
	sshConfig = ""
	privateKey = "private-key"
	os.Setenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG", "")
	os.Setenv("FSERVE_TEST_USE_PRIVATE_KEY", "1")
	if err := setPackageSource(ctx, testSDK, repoPort, name, deviceName, deviceIP, sshConfig, privateKey); err != nil {
		t.Fatal(err)
	}

}

/*
This "test" is used to mock the command line tools invoked by fserve.
The method "helperCommandForFServe" replaces exec.Command and runs
this test inplace of the command.

This approach to mocking out executables is  based on  exec_test.go.

*/
func TestFakeFServe(t *testing.T) {
	t.Helper()
	if os.Getenv("GO_WANT_HELPER_PROCESS") != "1" {
		return
	}
	defer os.Exit(0)

	args := os.Args
	for len(args) > 0 {
		if args[0] == "--" {
			args = args[1:]
			break
		}
		args = args[1:]
	}
	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "No command\n")
		os.Exit(2)
	}
	// Check the command line
	cmd, args := args[0], args[1:]
	switch filepath.Base(cmd) {
	case "device-finder":
		fakeDeviceFinder(args)
	case "pgrep":
		fakePgrep(args)
	case "ps":
		fakePS(args)
	case "pm":
		fakePM(args)
	case "gsutil":
		fakeGSUtil(args)
	case "ssh":
		fakeSSH(args)
	case "ssh-keygen":
		fakeSSHKeyGen(args)
	default:
		fmt.Fprintf(os.Stderr, "Unexpected command %v", cmd)
		os.Exit(1)
	}
}

func fakeDeviceFinder(args []string) {
	expected := []string{}
	expectedResolveArgs := []string{"resolve", "-device-limit", "1", "-ipv4=false", "test-device"}
	if args[0] == "resolve" {
		expected = expectedResolveArgs
		fmt.Println(resolvedAddr)
	}
	ok := len(expected) == len(args)
	for i := range args {
		if strings.Contains(expected[i], "*") {
			expectedPattern := regexp.MustCompile(expected[i])
			ok = ok && expectedPattern.MatchString(args[i])
		} else {
			ok = ok && args[i] == expected[i]
		}
	}
	if !ok {
		fmt.Fprintf(os.Stderr, "unexpected ssh args  %v exepected %v", args, expected)
		os.Exit(1)
	}
}

func fakeSSH(args []string) {
	expected := []string{}
	expectedHostConnection := []string{}
	expectedSetSource := []string{}
	privateKeyArgs := []string{"-i", "private-key"}

	sshConfigMatch := "/.*/sshconfig"
	if os.Getenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG") != "" {
		sshConfigMatch = "custom-sshconfig"
	}
	sshConfigArgs := []string{"-F", sshConfigMatch}

	hostaddr := "fe80::c0ff:eeee:fefe:c000%eth1"
	expectedURL := "http://[fe80::c0ff:eeee:fefe:c000%25eth1]:8083/config.json"
	targetaddr := resolvedAddr
	targetIndex := 2

	expectedHostConnection = append(expectedHostConnection, sshConfigArgs...)
	expectedSetSource = append(expectedSetSource, sshConfigArgs...)

	if os.Getenv("FSERVE_TEST_USE_PRIVATE_KEY") != "" {
		targetIndex = 4
		expectedHostConnection = append(expectedHostConnection, privateKeyArgs...)
		expectedSetSource = append(expectedSetSource, privateKeyArgs...)

	}

	if args[targetIndex] == resolvedAddr {
		hostaddr = "fe80::c0ff:eeee:fefe:c000%eth1"
		expectedURL = "http://[fe80::c0ff:eeee:fefe:c000%25eth1]:8083/config.json"

	} else {
		targetaddr = args[targetIndex]
		hostaddr = "10.10.1.12"
		expectedURL = "http://10.10.1.12:8083/config.json"

	}

	expectedHostConnection = append(expectedHostConnection, targetaddr, "echo", "$SSH_CONNECTION")
	expectedSetSource = append(expectedSetSource, targetaddr, "amber_ctl", "add_src", "-n", "devhost", "-f", expectedURL)

	if args[len(args)-1] == "$SSH_CONNECTION" {
		expected = expectedHostConnection
		fmt.Printf("%v 54545 fe80::c00f:f0f0:eeee:cccc 22\n", hostaddr)
	} else if args[len(args)-1] == expectedURL {
		expected = expectedSetSource
	}

	ok := len(args) == len(expected)
	if ok {
		for i := range args {
			if strings.Contains(expected[i], "*") {
				expectedPattern := regexp.MustCompile(expected[i])
				ok = ok && expectedPattern.MatchString(args[i])
			} else {
				ok = ok && args[i] == expected[i]
			}
		}
	}
	if !ok {
		fmt.Fprintf(os.Stderr, "unexpected ssh args  %v expected %v", args, expected)
		os.Exit(1)
	}
}

func fakeSSHKeyGen(args []string) {
	expected := []string{}

	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "Expected arguments to ssh-keygen\n")
		os.Exit(1)
	}
	switch args[0] {
	case "-P":
		expected = []string{"-P", "", "-t", "ed25519", "-f", "/.*/_TEMP_HOME/.ssh/fuchsia_ed25519", "-C", "testuser@testhost generated by Fuchsia GN SDK"}
	case "-y":
		expected = []string{"-y", "-f", "/.*/_TEMP_HOME/.ssh/fuchsia_ed25519"}
		fmt.Println("ssh-ed25519 AAAAC3NzaC1lTESTNTE5AAAAILxVYY7Q++kWUCmlfK1B6JQ9FPRaee05Te/PSHWVTeST testuser@test-host generated by Fuchsia GN SDK")
	}
	ok := len(args) == len(expected)
	if ok {
		for i := range args {
			if strings.Contains(expected[i], "*") {
				expectedPattern := regexp.MustCompile(expected[i])
				ok = ok && expectedPattern.MatchString(args[i])
			} else {
				ok = ok && args[i] == expected[i]
			}
		}
	}
	if !ok {
		fmt.Fprintf(os.Stderr, "unexpected ssh-keygen args  %v. Expected %v", args, expected)
		os.Exit(1)
	}
}

func fakeGSUtil(args []string) {
	expected := []string{}
	expectedLS := []string{"ls", "gs://test-bucket/path/on/GCS/theImage.tgz"}
	expectsedCP := []string{"cp", "gs://test-bucket/path/on/GCS/theImage.tgz", "/.*/theImage.tgz"}

	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "Expected arguments to gsutil\n")
		os.Exit(1)
	}
	switch args[0] {
	case "ls":
		expected = expectedLS
	case "cp":
		if os.Getenv("FSERVE_TEST_ASSERT_NO_DOWNLOAD") != "" {
			fmt.Fprintf(os.Stderr, "Unexpected call to gsutil cp: %v\n", args)
			os.Exit(1)
		}
		if os.Getenv("FSERVE_TEST_COPY_FAILS") != "" {
			fmt.Fprintf(os.Stderr, "BucketNotFoundException: 404 %v bucket does not exist.", args[1])
			os.Exit(2)
		}
		expected = expectsedCP
		// Copy the test data to the expected path.
		testRoot := os.Getenv("FSERVE_TEST_TESTROOT")
		if testRoot != "" {
			testdata := filepath.Join(testRoot, "testdata", "testdata.tgz")
			if err := copyFile(testdata, args[2]); err != nil {
				fmt.Fprintf(os.Stderr, "Error linking testdata: %v\n", err)
				os.Exit(1)
			}
		}
	}

	ok := len(args) == len(expected)
	if ok {
		for i := range args {
			if strings.Contains(expected[i], "*") {
				expectedPattern := regexp.MustCompile(expected[i])
				ok = ok && expectedPattern.MatchString(args[i])
			} else {
				ok = ok && args[i] == expected[i]
			}
		}
	}
	if !ok {
		fmt.Fprintf(os.Stderr, "unexpected gsutil args  %v. Expected %v", args, expected)
		os.Exit(1)
	}
}

func fakePM(args []string) {
	expected := []string{"serve", "-repo", "/fake/repo/path", "-l", ":8083"}
	ok := len(args) == len(expected)
	if ok {
		for i := range args {
			ok = ok && args[i] == expected[i]
		}
	}
	if !ok {
		fmt.Fprintf(os.Stderr, "unexpected pm args  %v. Expected %v", args, expected)
		os.Exit(1)
	}
}

func fakePgrep(args []string) {
	if os.Getenv("FSERVE_TEST_PGREP_ERROR") == "1" {
		fmt.Fprintf(os.Stderr, "Expected error\n")
		os.Exit(1)
	}
	if args[0] == "pm" {
		if os.Getenv("FSERVE_TEST_NO_SERVERS") == "1" {
			os.Exit(0)
		} else {
			// return 3 pm instances
			fmt.Printf(`1000
		2000
		3000`)
			os.Exit(0)
		}
	}
	fmt.Fprintf(os.Stderr, "unexpected pgrep args  %v", args)
	os.Exit(1)
}

func fakePS(args []string) {
	if os.Getenv("FSERVE_TEST_PS_ERROR") == "1" {
		fmt.Fprintf(os.Stderr, "Expected error\n")
		os.Exit(1)
	}
	fmt.Println("    PID TTY      STAT   TIME COMMAND")
	for _, arg := range args {
		switch arg {
		case "1000":
			// some internal process
			fmt.Println("1000 ?       I<     0:00  [tpm_dev_wq]")
		case "2000":
			// pm on port 8083
			fmt.Println("2000  pts/0    Sl     0:00 /sdk/path/tools/x64/pm serve -repo /home/developer/.fuchsia/packages/amber-files -l :8083")
		case "3000":
			// pm on port 8084
			fmt.Println("3000  pts/0    Sl     0:00 /sdk/path/tools/x64/pm serve -repo /home/developer/.fuchsia/packages/amber-files -l :8084 -q")
		}
	}
}

type testProcess struct {
	Pid int
}

func (proc testProcess) Kill() error {
	// This mock only kills pid == 2000
	if proc.Pid != 2000 {
		return fmt.Errorf("Unexpected pid %v in Kill", proc.Pid)
	}
	return nil
}

func mockedFindProcess(pid int) (osProcess, error) {
	proc := testProcess{Pid: pid}

	return &proc, nil
}

func mockWait4NoError(pid int, wstatus *syscall.WaitStatus, flags int, usage *syscall.Rusage) (int, error) {
	return 0, nil
}

func mockWait4WithError(pid int, wstatus *syscall.WaitStatus, flags int, usage *syscall.Rusage) (int, error) {
	// set wstatus to exited with code 1.
	*wstatus = syscall.WaitStatus(0x100)
	return 0, nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	if err != nil {
		return err
	}
	return out.Close()
}
