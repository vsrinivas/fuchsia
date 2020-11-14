// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sdkcommon

import (
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

const resolvedAddr = "fe80::c0ff:eee:fe00:4444%en0"

// See exec_test.go for details, but effectively this runs the function called TestHelperProcess passing
// the args.
func helperCommandForSDKCommon(command string, s ...string) (cmd *exec.Cmd) {
	//testenv.MustHaveExec(t)

	cs := []string{"-test.run=TestFakeSDKCommon", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

func TestGetAvailableImages(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	ExecLookPath = func(cmd string) (string, error) { return filepath.Join("mocked", cmd), nil }
	defer func() {
		ExecCommand = exec.Command
		ExecLookPath = exec.LookPath
	}()
	testSDK := SDKProperties{
		DataPath: t.TempDir(),
	}
	version := "test-version"
	bucket := ""

	images, err := testSDK.GetAvailableImages(version, bucket)
	if err != nil {
		t.Fatal(err)
	}
	if len(images) != 2 {
		t.Fatalf("Expected 2 images, got %v: %v", len(images), images)
	}

	bucket = "private-bucket"
	images, err = testSDK.GetAvailableImages(version, bucket)
	if err != nil {
		t.Fatal(err)
	}
	if len(images) != 4 {
		t.Fatalf("Expected 4 images, got %v: %v", len(images), images)
	}

	bucket = ""
	version = "unknown"
	images, err = testSDK.GetAvailableImages(version, bucket)
	if err != nil {
		expected := "CommandException: One or more URLs matched no objects.: exit status 2"
		actual := fmt.Sprintf("%v", err)
		if actual != expected {
			t.Fatalf("Expected exception [%v] got [%v]", expected, actual)
		}
	} else {
		t.Fatal("Expected exception, but did not get one")
	}
}

func TestGetAddressByName(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
	}()
	testSDK := SDKProperties{
		DataPath: t.TempDir(),
	}
	deviceName := "test-device"
	_, err := testSDK.GetAddressByName(deviceName)
	if err != nil {
		t.Fatal(err)
	}
	deviceName = "unknown-device"
	_, err = testSDK.GetAddressByName(deviceName)
	if err != nil {
		expected := "resolve.go:76: no devices found for domains: [unknown-device]: exit status 2"
		actual := fmt.Sprintf("%v", err)
		if actual != expected {
			t.Fatalf("Expected exception [%v] got [%v]", expected, actual)
		}
	} else {
		t.Fatal("Expected exception, but did not get one")
	}

}

func TestRunSSHCommand(t *testing.T) {
	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0755); err != nil {
		t.Fatal(err)
	}
	ExecCommand = helperCommandForSDKCommon
	GetUserHomeDir = mockedUserProperty(homeDir)
	GetUsername = mockedUserProperty("testuser")
	GetHostname = mockedUserProperty("test-host")
	defer func() {
		ExecCommand = exec.Command
		GetUserHomeDir = DefaultGetUserHomeDir
		GetUsername = DefaultGetUsername
		GetHostname = DefaultGetHostname
	}()
	testSDK := SDKProperties{
		DataPath: t.TempDir(),
	}

	targetAddress := resolvedAddr
	customSSHConfig := ""
	privateKey := ""
	args := []string{"echo", "$SSH_CONNECTION"}
	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, args); err != nil {
		t.Fatal(err)
	}

	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, args); err != nil {
		t.Fatal(err)
	}

	customSSHConfig = "custom-sshconfig"
	os.Setenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG", "1")
	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, args); err != nil {
		t.Fatal(err)
	}

	customSSHConfig = ""
	privateKey = "private-key"
	os.Setenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG", "")
	os.Setenv("FSERVE_TEST_USE_PRIVATE_KEY", "1")
	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, args); err != nil {
		t.Fatal(err)
	}
}

func TestCheckSSHConfig(t *testing.T) {
	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0755); err != nil {
		t.Fatal(err)
	}
	ExecCommand = helperCommandForSDKCommon
	GetUserHomeDir = mockedUserProperty(homeDir)
	GetUsername = mockedUserProperty("testuser")
	GetHostname = mockedUserProperty("test-host")
	defer func() {
		ExecCommand = exec.Command
		GetUserHomeDir = DefaultGetUserHomeDir
		GetUsername = DefaultGetUsername
		GetHostname = DefaultGetHostname
	}()
	testSDK := SDKProperties{
		DataPath: t.TempDir(),
	}
	if err := checkSSHConfig(testSDK); err != nil {
		t.Fatal(err)
	}
}

func TestCheckSSHConfigExistingFiles(t *testing.T) {

	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0755); err != nil {
		t.Fatal(err)
	}
	ExecCommand = helperCommandForSDKCommon
	GetUserHomeDir = mockedUserProperty(homeDir)
	GetUsername = mockedUserProperty("testuser")
	GetHostname = mockedUserProperty("test-host")
	defer func() {
		ExecCommand = exec.Command
		GetUserHomeDir = DefaultGetUserHomeDir
		GetUsername = DefaultGetUsername
		GetHostname = DefaultGetHostname
	}()
	testSDK := SDKProperties{
		DataPath: t.TempDir(),
	}

	// Write out SSH keys and config
	data := []byte("Test SSH Key\n")
	sshDir := filepath.Join(homeDir, ".ssh")
	authFile := filepath.Join(sshDir, "fuchsia_authorized_keys")
	keyFile := filepath.Join(sshDir, "fuchsia_ed25519")
	sshConfigFile := getFuchsiaSSHConfigFile(testSDK)
	if err := os.MkdirAll(sshDir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(authFile, data, 0644); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(keyFile, data, 0644); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(sshConfigFile, []byte(sshConfigTag), 0644); err != nil {
		t.Fatal(err)
	}

	if err := checkSSHConfig(testSDK); err != nil {
		t.Fatal(err)
	}

	// Make sure they have not changed
	content, err := ioutil.ReadFile(authFile)
	if err != nil {
		t.Fatal(err)
	}
	if string(content) != string(data) {
		t.Fatalf("Expected test auth file to contain [%v], but contains [%v]", string(data), string(content))
	}
	content, err = ioutil.ReadFile(keyFile)
	if err != nil {
		t.Fatal(err)
	}
	if string(content) != string(data) {
		t.Fatalf("Expected test key file to contain [%v], but contains [%v]", string(data), string(content))
	}
	content, err = ioutil.ReadFile(sshConfigFile)
	if err != nil {
		t.Fatal(err)
	}
	if string(content) != sshConfigTag {
		t.Fatalf("Expected sshConfig file to contain [%v], but contains [%v]", string(data), string(content))
	}
}
func mockedUserProperty(value string) func() (string, error) {
	return func() (string, error) {
		return value, nil
	}
}

func TestFakeSDKCommon(t *testing.T) {
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
	case "gsutil":
		fakeGSUtil(args)
	case "device-finder":
		fakeDeviceFinder(args)
	case "ssh":
		fakeSSH(args)
	case "ssh-keygen":
		fakeSSHKeygen(args)
	default:
		fmt.Fprintf(os.Stderr, "Unexpected command %v", cmd)
		os.Exit(1)
	}
}

func fakeGSUtil(args []string) {
	expected := []string{}

	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "Expected arguments to gsutil\n")
		os.Exit(1)
	}
	switch args[0] {
	case "ls":
		switch args[1] {
		case "gs://fuchsia/development/test-version/images":
			expected = []string{args[0], args[1]}
			fmt.Printf("%v/image1.tgz\n", args[1])
			fmt.Printf("%v/image2.tgz\n", args[1])
		case "gs://private-bucket/development/test-version/images":
			expected = []string{args[0], args[1]}
			fmt.Printf("%v/priv-image3.tgz\n", args[1])
			fmt.Printf("%v/priv-image4.tgz\n", args[1])
		case "gs://fuchsia/development/unknown/images":
			expected = []string{args[0], args[1]}
			fmt.Fprintf(os.Stderr, "CommandException: One or more URLs matched no objects.")
			os.Exit(2)
		default:
			expected = []string{"ls", "gs://fuchsia/development/test-version/images"}
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

func fakeDeviceFinder(args []string) {
	expected := []string{}
	expectedResolveArgs := []string{"resolve", "-device-limit", "1", "-ipv4=false", "test-device"}
	if args[0] == "resolve" {
		expected = expectedResolveArgs
		if args[len(args)-1] == "test-device" {
			fmt.Println(resolvedAddr)
		} else {
			fmt.Fprintf(os.Stderr, "resolve.go:76: no devices found for domains: [%v]", args[len(args)-1])
			os.Exit(2)
		}
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

	} else {
		targetaddr = args[targetIndex]
		hostaddr = "10.10.1.12"
	}

	expectedHostConnection = append(expectedHostConnection, targetaddr, "echo", "$SSH_CONNECTION")

	if args[len(args)-1] == "$SSH_CONNECTION" {
		expected = expectedHostConnection
		fmt.Printf("%v 54545 fe80::c00f:f0f0:eeee:cccc 22\n", hostaddr)
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

func fakeSSHKeygen(args []string) {
	expectedPrivate := []string{"-P", "", "-t", "ed25519", "-f", "/.*/_TEMP_HOME/.ssh/fuchsia_ed25519", "-C", "testuser@test-host generated by Fuchsia GN SDK"}
	expectedPublic := []string{"-y", "-f", "/.*/_TEMP_HOME/.ssh/fuchsia_ed25519"}
	expected := []string{}
	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "expected ssh-keygen args")
		os.Exit(1)
	}
	if args[0] == "-P" {
		expected = expectedPrivate
	} else if args[0] == "-y" {
		expected = expectedPublic
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
		fmt.Fprintf(os.Stderr, "unexpected ssh-keygen args  %v exepected %v", args, expected)
		os.Exit(1)
	}
}
