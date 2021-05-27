// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sdkcommon

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

const resolvedAddr = "fe80::c0ff:eee:fe00:4444%en0"

var allSSHOptions = []string{
	"FSERVE_TEST_USE_CUSTOM_SSH_CONFIG",
	"FSERVE_TEST_USE_PRIVATE_KEY",
	"FSERVE_TEST_USE_CUSTOM_SSH_PORT",
	"SFTP_TO_TARGET",
}

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

func TestNewSDK(t *testing.T) {
	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0o700); err != nil {
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
	testSDK, err := NewWithDataPath(tempDir)
	if err != nil {
		t.Fatal(err)
	}
	expectedPath := tempDir
	if testSDK.GetSDKDataPath() != expectedPath {
		t.Fatalf("NewWithDataPath datapath mismatch: expected: %v actual: %v",
			expectedPath, testSDK.GetSDKDataPath())
	}

	testSDK, err = New()
	if err != nil {
		t.Fatal(err)
	}
	expectedPath = tempDir + "/_TEMP_HOME/.fuchsia"
	if testSDK.GetSDKDataPath() != expectedPath {
		t.Fatalf("New datapath mismatch: expected: %v actual: %v",
			expectedPath, testSDK.GetSDKDataPath())
	}
}

func TestGetAvailableImages(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	ExecLookPath = func(cmd string) (string, error) { return filepath.Join("mocked", cmd), nil }
	defer func() {
		ExecCommand = exec.Command
		ExecLookPath = exec.LookPath
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
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

	bucket = "new"
	version = "multi-version**"
	images, err = testSDK.GetAvailableImages(version, bucket)
	if err != nil {
		t.Fatal(err)
	}
	if len(images) != 4 {
		t.Fatalf("Expected 4 images, got %v: %v", len(images), images)
	}
	expectedImages := []GCSImage{
		{
			Bucket:  "new",
			Version: "multi-version1",
			Name:    "priv-image1",
		}, {
			Bucket:  "new",
			Version: "multi-version2",
			Name:    "priv-image1",
		},
		{
			Bucket:  "fuchsia",
			Version: "multi-version1",
			Name:    "image1",
		}, {
			Bucket:  "fuchsia",
			Version: "multi-version2",
			Name:    "image1",
		},
	}
	if diff := cmp.Diff(expectedImages, images, cmpopts.SortSlices(func(a, b GCSImage) bool {
		return a.Bucket == b.Bucket && a.Version == b.Version && a.Name == b.Name
	})); diff != "" {
		t.Errorf("GetAvailableImages() mismatch (-want +got):\n%s", diff)
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

func TestGetAddressByNameDeviceFinder(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
		os.Unsetenv("FUCHSIA_DISABLED_ffx_discovery")
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	os.Setenv("FUCHSIA_DISABLED_ffx_discovery", "1")
	deviceName := "test-device-df"
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

func TestGetAddressByNameFfx(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	deviceName := "test-device"
	_, err := testSDK.GetAddressByName(deviceName)
	if err != nil {
		t.Fatal(err)
	}

	deviceName = "unknown-test-device"
	_, err = testSDK.GetAddressByName(deviceName)
	if err != nil {
		expected := "No devices found.: exit status 2"
		actual := fmt.Sprintf("%v", err)
		if actual != expected {
			t.Fatalf("Expected exception [%v] got [%v]", expected, actual)
		}
	} else {
		t.Fatal("Expected exception, but did not get one")
	}
}

func compareFuchsiaDevices(f1, f2 *FuchsiaDevice) bool {
	return cmp.Equal(f1.IpAddr, f2.IpAddr) && cmp.Equal(f1.Name, f2.Name)
}

func TestDeviceString(t *testing.T) {
	device := &FuchsiaDevice{
		IpAddr: "123-123-123-123",
		Name:   "test-device",
	}
	expectedOutput := "123-123-123-123 test-device"
	actual := device.String()
	if actual != expectedOutput {
		t.Errorf("Expected String [%v] got [%v]", expectedOutput, actual)
	}
}

func TestFindDeviceByNameFfx(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
	}()
	testSDK, err := NewWithDataPath(t.TempDir())
	if err != nil {
		t.Error(err)
	}

	deviceName := "test-device"
	expectedFuchsiaDevice := &FuchsiaDevice{
		IpAddr: "123-123-123-123",
		Name:   "test-device",
	}
	output, err := testSDK.FindDeviceByName(deviceName)
	if err != nil {
		t.Error(err)
	}
	if d := cmp.Diff(expectedFuchsiaDevice, output, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("findDeviceByName mismatch: (-want +got):\n%s", d)
	}

	deviceName = "unknown-device"
	_, err = testSDK.FindDeviceByName(deviceName)
	if err != nil {
		expected := "no device with device name unknown-device found"
		actual := fmt.Sprintf("%v", err)
		if actual != expected {
			t.Errorf("Expected exception [%v] got [%v]", expected, actual)
		}
	} else {
		t.Error("Expected exception, but did not get one")
	}
}

func TestFindDeviceByNameDeviceFinder(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
		os.Unsetenv("FUCHSIA_DISABLED_ffx_discovery")
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	os.Setenv("FUCHSIA_DISABLED_ffx_discovery", "1")
	deviceName := "test-device-df"
	expectedFuchsiaDevice := &FuchsiaDevice{
		IpAddr: "123-123-123-1df",
		Name:   "test-device-df",
	}
	output, err := testSDK.FindDeviceByName(deviceName)
	if err != nil {
		t.Error(err)
	}
	if d := cmp.Diff(expectedFuchsiaDevice, output, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("findDeviceByName mismatch: (-want +got):\n%s", d)
	}

	deviceName = "unknown-device"
	_, err = testSDK.FindDeviceByName(deviceName)
	if err != nil {
		expected := "no device with device name unknown-device found"
		actual := fmt.Sprintf("%v", err)
		if actual != expected {
			t.Errorf("Expected exception [%v] got [%v]", expected, actual)
		}
	} else {
		t.Error("Expected exception, but did not get one")
	}
}

func TestFindDeviceByIPFfx(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	ipAddr := "456-456-456-456"
	expectedFuchsiaDevice := &FuchsiaDevice{
		IpAddr: "456-456-456-456",
		Name:   "another-test-device",
	}
	output, err := testSDK.FindDeviceByIP(ipAddr)
	if err != nil {
		t.Error(err)
	}
	if d := cmp.Diff(expectedFuchsiaDevice, output, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("findDeviceByIP mismatch: (-want +got):\n%s", d)
	}

	ipAddr = "999-999-999-999"
	_, err = testSDK.FindDeviceByIP(ipAddr)
	expected := "no device with IP address 999-999-999-999 found"
	actual := fmt.Sprintf("%v", err)
	if actual != expected {
		t.Errorf("Expected exception [%v] got [%v]", expected, actual)
	}
}

func TestFindDeviceByIPDeviceFinder(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
		os.Unsetenv("FUCHSIA_DISABLED_ffx_discovery")
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	os.Setenv("FUCHSIA_DISABLED_ffx_discovery", "1")
	ipAddr := "456-456-456-4df"
	expectedFuchsiaDevice := &FuchsiaDevice{
		IpAddr: "456-456-456-4df",
		Name:   "another-test-device-df",
	}
	output, err := testSDK.FindDeviceByIP(ipAddr)
	if err != nil {
		t.Error(err)
	}
	if d := cmp.Diff(expectedFuchsiaDevice, output, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("findDeviceByIP mismatch: (-want +got):\n%s", d)
	}

	ipAddr = "999-999-999-999"
	_, err = testSDK.FindDeviceByIP(ipAddr)
	expected := "no device with IP address 999-999-999-999 found"
	actual := fmt.Sprintf("%v", err)
	if actual != expected {
		t.Errorf("Expected exception [%v] got [%v]", expected, actual)
	}
}

func TestListDevicesFfx(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	expectedFuchsiaDevice := []*FuchsiaDevice{
		{
			IpAddr: "123-123-123-123",
			Name:   "test-device",
		}, {
			IpAddr: "456-456-456-456",
			Name:   "another-test-device",
		},
	}
	output, err := testSDK.ListDevices()
	if err != nil {
		t.Error(err)
	}
	if d := cmp.Diff(expectedFuchsiaDevice, output, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
	}
}

func TestListDevicesDeviceFinder(t *testing.T) {
	ExecCommand = helperCommandForSDKCommon
	defer func() {
		ExecCommand = exec.Command
		os.Unsetenv("FUCHSIA_DISABLED_ffx_discovery")
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	os.Setenv("FUCHSIA_DISABLED_ffx_discovery", "1")
	expectedFuchsiaDevice := []*FuchsiaDevice{
		{
			IpAddr: "123-123-123-1df",
			Name:   "test-device-df",
		}, {
			IpAddr: "456-456-456-4df",
			Name:   "another-test-device-df",
		},
	}
	output, err := testSDK.ListDevices()
	if err != nil {
		t.Error(err)
	}
	if d := cmp.Diff(expectedFuchsiaDevice, output, cmp.Comparer(compareFuchsiaDevices)); d != "" {
		t.Errorf("listDevices mismatch: (-want +got):\n%s", d)
	}
}

func TestRunSSHCommand(t *testing.T) {
	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0o700); err != nil {
		t.Fatal(err)
	}
	ExecCommand = helperCommandForSDKCommon
	GetUserHomeDir = mockedUserProperty(homeDir)
	GetUsername = mockedUserProperty("testuser")
	GetHostname = mockedUserProperty("test-host")
	defer func() {
		for _, option := range allSSHOptions {
			os.Setenv(option, "")
		}
		ExecCommand = exec.Command
		GetUserHomeDir = DefaultGetUserHomeDir
		GetUsername = DefaultGetUsername
		GetHostname = DefaultGetHostname
	}()
	testSDK, err := NewWithDataPath(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}

	tests := []struct {
		customSSHConfig string
		privateKey      string
		sshPort         string
		verbose         bool
		args            []string
		options         map[string]string
	}{
		{
			customSSHConfig: "",
			privateKey:      "",
			sshPort:         "",
			verbose:         false,
			args:            []string{"echo", "$SSH_CONNECTION"},
		},
		{
			customSSHConfig: "custom-sshconfig",
			privateKey:      "",
			sshPort:         "",
			verbose:         false,
			args:            []string{"echo", "$SSH_CONNECTION"},
			options:         map[string]string{"FSERVE_TEST_USE_CUSTOM_SSH_CONFIG": "1"},
		},
		{
			customSSHConfig: "",
			privateKey:      "private-key",
			sshPort:         "",
			verbose:         false,
			args:            []string{"echo", "$SSH_CONNECTION"},
			options:         map[string]string{"FSERVE_TEST_USE_PRIVATE_KEY": "1"},
		},
		{
			customSSHConfig: "",
			privateKey:      "",
			sshPort:         "",
			verbose:         false,
			args:            []string{"echo", "$SSH_CONNECTION"},
		},
		{
			customSSHConfig: "",
			privateKey:      "",
			sshPort:         "1022",
			verbose:         false,
			args:            []string{"echo", "$SSH_CONNECTION"},
			options:         map[string]string{"FSERVE_TEST_USE_CUSTOM_SSH_PORT": "1"},
		},
	}

	targetAddress := resolvedAddr
	for i, test := range tests {
		for _, option := range allSSHOptions {
			os.Setenv(option, "")
		}
		for option, value := range test.options {
			os.Setenv(option, value)
		}
		if _, err := testSDK.RunSSHCommandWithPort(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.verbose, test.args); err != nil {
			t.Errorf("RunSSHCommandWithPort %d error: %v", i, err)
		}
		if err := testSDK.RunSSHShell(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.verbose, test.args); err != nil {
			t.Errorf("TestRunSSHShell (using port) %d error: %v", i, err)
		}
		if _, err := testSDK.RunSSHCommand(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.verbose, test.args); err != nil {
			t.Errorf("TestRunSSHCommand %d error: %v", i, err)
		}
		if err := testSDK.RunSSHShell(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.verbose, test.args); err != nil {
			t.Errorf("TestRunSSHShell %d error: %v", i, err)
		}
	}
}

func TestRunRunSFTPCommand(t *testing.T) {
	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0o700); err != nil {
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
		for _, option := range allSSHOptions {
			os.Setenv(option, "")
		}
	}()
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}

	tests := []struct {
		customSSHConfig string
		privateKey      string
		sshPort         string
		to_target       bool
		options         map[string]string
	}{
		{
			customSSHConfig: "",
			privateKey:      "",
			sshPort:         "",
			to_target:       false,
		},
		{
			customSSHConfig: "",
			privateKey:      "",
			sshPort:         "",
			to_target:       true,
			options:         map[string]string{"SFTP_TO_TARGET": "1"},
		},
		{
			customSSHConfig: "custom-sshconfig",
			privateKey:      "",
			sshPort:         "",
			to_target:       false,
			options:         map[string]string{"FSERVE_TEST_USE_CUSTOM_SSH_CONFIG": "1"},
		},
		{
			customSSHConfig: "",
			privateKey:      "private-key",
			sshPort:         "",
			to_target:       true,
			options: map[string]string{
				"SFTP_TO_TARGET":              "1",
				"FSERVE_TEST_USE_PRIVATE_KEY": "1",
			},
		},
		{
			customSSHConfig: "",
			privateKey:      "",
			sshPort:         "",
			to_target:       false,
		},
		{
			customSSHConfig: "",
			privateKey:      "",
			sshPort:         "1022",
			to_target:       false,
			options:         map[string]string{"FSERVE_TEST_USE_CUSTOM_SSH_PORT": "1"},
		},
	}
	targetAddress := resolvedAddr
	src := "/some/src/file"
	dst := "/dst/file"

	for i, test := range tests {
		for _, option := range allSSHOptions {
			os.Setenv(option, "")
		}
		for option, value := range test.options {
			os.Setenv(option, value)
		}
		if err := testSDK.RunSFTPCommandWithPort(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.to_target, src, dst); err != nil {
			t.Errorf("RunSFTPCommandWithPort %d error: %v", i, err)
		}
		if err := testSDK.RunSFTPCommand(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.to_target, src, dst); err != nil {
			t.Errorf("TestRunSSHShell (using port) %d error: %v", i, err)
		}
	}
}

func TestCheckSSHConfig(t *testing.T) {
	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0o700); err != nil {
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
		dataPath: t.TempDir(),
	}
	if err := checkSSHConfig(testSDK); err != nil {
		t.Fatal(err)
	}
}

func TestCheckSSHConfigExistingFiles(t *testing.T) {

	tempDir := t.TempDir()
	homeDir := filepath.Join(tempDir, "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0o700); err != nil {
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
		dataPath: t.TempDir(),
	}

	// Write out SSH keys and config
	data := []byte("Test SSH Key\n")
	sshDir := filepath.Join(homeDir, ".ssh")
	authFile := filepath.Join(sshDir, "fuchsia_authorized_keys")
	keyFile := filepath.Join(sshDir, "fuchsia_ed25519")
	sshConfigFile := getFuchsiaSSHConfigFile(testSDK)
	if err := os.MkdirAll(sshDir, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(authFile, data, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(keyFile, data, 0o600); err != nil {
		t.Fatal(err)
	}
	if err := ioutil.WriteFile(sshConfigFile, []byte(sshConfigTag), 0o600); err != nil {
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

func TestGetDefaultDeviceName(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = helperCommandForGetFuchsiaProperty
	defer func() { ExecCommand = exec.Command }()

	val, err := sdk.GetDefaultDeviceName()
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}
	if val != "fake-target-device-name" {
		t.Fatalf("Unexpected default device name. Expected fake-target-device-name got: %v", val)
	}
	ExecCommand = helperCommandForNoDefaultDevice
	val, err = sdk.GetDefaultDeviceName()
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}
	if val != "" {
		t.Fatalf("Unexpected default device name. Expected no name got: %v", val)
	}
}

func TestGetFuchsiaProperty(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = helperCommandForGetFuchsiaProperty
	defer func() { ExecCommand = exec.Command }()

	testData := []struct {
		device, property, expected, errString string
	}{
		{"", "device-name", "fake-target-device-name", ""},
		{"some-other-device", "device-name", "", ""},
		{"some-other-device", "random-property", "", "Could not find property some-other-device.random-property"},
		{"", "random-property", "", "Could not find property fake-target-device-name.random-property"},
		{"", PackageRepoKey, "fake-target-device-name/packages/amber-files", ""},
		{"another-target-device-name", PackageRepoKey, "another-target-device-name/packages/amber-files", ""},
	}

	for i, data := range testData {
		t.Run(fmt.Sprintf("TestGetFuchsiaProperty.%d", i), func(t *testing.T) {
			val, err := sdk.GetFuchsiaProperty(data.device, data.property)
			if err != nil {
				if data.errString == "" {
					t.Fatalf("Unexpected error getting property %s.%s: %v", data.device, data.property, err)
				} else if !strings.Contains(fmt.Sprintf("%v", err), data.errString) {
					t.Errorf("Expected error message %v not found in error %v", data.errString, err)
				}
			} else {
				if val != data.expected {
					t.Errorf("GetFuchsiaProperyFailed %s.%s = %s, expected %s", data.device, data.property, val, data.expected)
				}
				if data.errString != "" {
					t.Errorf("Expected error %v, but got no error", data.errString)
				}
			}
		})
	}
}

func TestGetDeviceConfigurations(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = helperCommandForGetFuchsiaProperty
	defer func() { ExecCommand = exec.Command }()

	val, err := sdk.GetDeviceConfigurations()
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}
	if len(val) != 2 {
		t.Errorf("TestGetDeviceConfigurations expected 2 devices: %v", val)
	}
}

func TestGetDeviceConfiguration(t *testing.T) {
	sdk := SDKProperties{}
	ExecCommand = helperCommandForGetFuchsiaProperty
	defer func() { ExecCommand = exec.Command }()

	const deviceName string = "another-target-device-name"
	val, err := sdk.GetDeviceConfiguration(deviceName)
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}
	if val.DeviceName != deviceName {
		t.Errorf("TestGetDeviceConfiguration failed. Expected configuration for %v:  %v", deviceName, val)
	}

	val, err = sdk.GetDeviceConfiguration("unknown-device")
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}
	if val.DeviceName != "" {
		t.Errorf("TestGetDeviceConfiguration failed. Expected empty configuration for %v:  %v", "unknown-device", val)
	}
}

func TestSaveDeviceConfiguration(t *testing.T) {
	sdk := SDKProperties{}
	ExecCommand = helperCommandForSetTesting
	defer func() { ExecCommand = exec.Command }()

	tests := []struct {
		currentDevice  DeviceConfig
		newDevice      DeviceConfig
		expectedValues map[string]string
	}{
		{
			newDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "new/device/repo",
				SSHPort:     "22",
			},
			expectedValues: map[string]string{
				"DeviceConfiguration.new-device-name.device-name":  "new-device-name",
				"DeviceConfiguration.new-device-name.package-port": "8000",
				"DeviceConfiguration.new-device-name.image":        "image-name",
				"DeviceConfiguration.new-device-name.bucket":       "buck-name",
				"DeviceConfiguration.new-device-name.package-repo": "new/device/repo",
				"DeviceConfiguration.new-device-name.device-ip":    "1.1.1.1",
				// Since ssh port is the default, it should be cleared.
				"DeviceConfiguration.new-device-name.ssh-port": "",
			},
		},
		{
			currentDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "22",
			},
			newDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "22",
			},
			expectedValues: map[string]string{
				// Device name is always written
				"DeviceConfiguration.new-device-name.device-name": "new-device-name",
				// Since ssh port is the default, it should be cleared.
				"DeviceConfiguration.new-device-name.ssh-port": "",
			},
		},
		{
			currentDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "22",
			},
			newDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "22",
				IsDefault:   true,
			},
			expectedValues: map[string]string{
				// Device name is always written
				"DeviceConfiguration.new-device-name.device-name": "new-device-name",
				// Since ssh port is the default, it should be cleared.
				"DeviceConfiguration.new-device-name.ssh-port": "",
				"DeviceConfiguration._DEFAULT_DEVICE_":         "new-device-name",
			},
		},
		{
			currentDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "22",
			},
			newDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "8022",
			},
			expectedValues: map[string]string{
				// Device name is always written
				"DeviceConfiguration.new-device-name.device-name": "new-device-name",
				"DeviceConfiguration.new-device-name.ssh-port":    "8022",
			},
		},
		{
			currentDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "8022",
			},
			newDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "8022",
			},
			expectedValues: map[string]string{
				// Device name is always written
				"DeviceConfiguration.new-device-name.device-name": "new-device-name",
			},
		},
		{
			currentDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "existing/repo",
				SSHPort:     "8022",
			},
			newDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "",
				Image:       "custom-image",
				Bucket:      "",
				PackagePort: "8000",
				PackageRepo: "",
				SSHPort:     "8022",
			},
			expectedValues: map[string]string{
				// Device name is always written
				"DeviceConfiguration.new-device-name.device-name":  "new-device-name",
				"DeviceConfiguration.new-device-name.bucket":       "",
				"DeviceConfiguration.new-device-name.device-ip":    "",
				"DeviceConfiguration.new-device-name.image":        "custom-image",
				"DeviceConfiguration.new-device-name.package-repo": "",
			},
		},
	}
	for i, test := range tests {
		t.Run(fmt.Sprintf("TestSaveDeviceConfiguration %v", i), func(t *testing.T) {
			expectedData, err := json.Marshal(test.expectedValues)
			if err != nil {
				t.Fatalf("unexpected err %v", err)
			}
			currentData, err := json.Marshal(test.currentDevice)
			if err != nil {
				t.Fatalf("unexpected err %v", err)
			}
			os.Setenv("TEST_EXPECTED_SET_DATA", string(expectedData))
			os.Setenv("TEST_CURRENT_DEVICE_DATA", string(currentData))
			err = sdk.SaveDeviceConfiguration(test.newDevice)
			if err != nil {
				t.Fatalf("unexpected err %v", err)
			}
		})
	}
}

func TestRemoveDeviceConfiguration(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = helperCommandForRemoveTesting
	defer func() { ExecCommand = exec.Command }()

	deviceName := "old-device-name"

	err := sdk.RemoveDeviceConfiguration(deviceName)
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}

	err = sdk.RemoveDeviceConfiguration("unknown-device")
	if err == nil {
		t.Fatal("expected error but did not get one.")
	}
	expectedErrorMessage := "Error removing unknown-device configuration"
	if !strings.HasPrefix(fmt.Sprintf("%v", err), expectedErrorMessage) {
		t.Fatalf("Expected `%v` in error: %v ", expectedErrorMessage, err)
	}
}

var tempGlobalSettingsFile = ""

func TestInitProperties(t *testing.T) {
	sdk := SDKProperties{
		globalPropertiesFilename: "/some/file.json",
	}

	ExecCommand = helperCommandForInitEnv
	defer func() { ExecCommand = exec.Command }()

	tempGlobalSettingsFile = filepath.Join(t.TempDir(), "global-config.json")
	defer func() { tempGlobalSettingsFile = "" }()
	emptyFile, err := os.Create(tempGlobalSettingsFile)
	if err != nil {
		t.Fatal(err)
	}
	emptyFile.Close()

	err = initFFXGlobalConfig(sdk)
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}

	ExecCommand = helperCommandForInitEnvNoExistingFile
	err = initFFXGlobalConfig(sdk)
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}
}

func TestResolveTargetAddress(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = nil
	defer func() { ExecCommand = exec.Command }()

	tests := []struct {
		defaultDeviceName string
		deviceIP          string
		deviceName        string
		expectedAddress   string
		expectedError     string
		execHelper        func(command string, s ...string) (cmd *exec.Cmd)
	}{
		{
			deviceIP:        "",
			deviceName:      "",
			expectedAddress: "",
			expectedError: `invalid arguments. Need to specify --device-ip or --device-name or use fconfig to configure a default device.
Try running "ffx target list --format s" and then "fconfig set-device <device_name> --image <image_name> --default".`,
			execHelper: helperCommandForNoDefaultDevice,
		},
		{
			deviceIP:        resolvedAddr,
			deviceName:      "",
			expectedAddress: resolvedAddr,
			expectedError:   "",
			execHelper:      helperCommandForNoDefaultDevice,
		},
		{
			defaultDeviceName: "test-device",
			deviceIP:          "",
			deviceName:        "",
			expectedAddress:   resolvedAddr,
			expectedError:     "",
			execHelper:        helperCommandForGetFuchsiaProperty,
		},
		{
			defaultDeviceName: "another-test-device",
			deviceIP:          "",
			deviceName:        "test-device",
			expectedAddress:   resolvedAddr,
			expectedError:     "",
			execHelper:        helperCommandForGetFuchsiaProperty,
		},
		{
			defaultDeviceName: "test-device",
			deviceIP:          "",
			deviceName:        "unknown-test-device",
			expectedAddress:   "",
			expectedError: `cannot get target address for unknown-test-device.
Try running "ffx target list --format s" and verify the name matches in "fconfig get-all". No devices found.: exit status 2`,
			execHelper: helperCommandForGetFuchsiaProperty,
		},
	}
	for i, test := range tests {
		os.Setenv("TEST_DEFAULT_DEVICE_NAME", test.defaultDeviceName)
		ExecCommand = test.execHelper

		target, err := sdk.ResolveTargetAddress(test.deviceIP, test.deviceName)
		if err != nil {
			message := fmt.Sprintf("%v", err)
			if message != test.expectedError {
				t.Fatalf("Error '%v' did not match expected error '%v'", message, test.expectedError)
			}
		} else if test.expectedError != "" {
			t.Fatalf("Expected error '%v', but got no error", test.expectedError)
		}
		if target != test.expectedAddress {
			t.Fatalf("test case %v: target address '%v' did not match expected '%v'", i, target, test.expectedAddress)
		}
	}
}

func TestRunFFXDoctor(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = helperCommandForSetTesting
	defer func() { ExecCommand = exec.Command }()

	output, err := sdk.RunFFXDoctor()
	if err != nil {
		t.Fatalf("unexpected err %v", err)
	}

	expectedOutput := "Welcome to ffx doctor."
	if output != expectedOutput {
		t.Fatalf("Expected %v, got %v", expectedOutput, output)
	}
}

func TestMapToDeviceConfig(t *testing.T) {

	tests := []struct {
		jsonString   string
		deviceConfig DeviceConfig
		deviceName   string
	}{
		{
			jsonString: `{
				  "test-device1": {
					"bucket": "",
					"device-ip": "",
					"device-name": "test-device1",
					"image": "",
					"package-port": "",
					"package-repo": "",
					"ssh-port": ""
				  }
				}
				`,
			deviceName: "test-device1",
			deviceConfig: DeviceConfig{
				DeviceName: "test-device1",
				IsDefault:  false,
			},
		},
		{
			jsonString: `{
				  "test-device1": {
					"bucket": "",
					"device-ip": "localhost",
					"device-name": "test-device1",
					"image": "",
					"package-port": 8888,
					"package-repo": "",
					"ssh-port": 1022
				  }
				}
				`,
			deviceName: "test-device1",
			deviceConfig: DeviceConfig{
				DeviceName:  "test-device1",
				DeviceIP:    "localhost",
				SSHPort:     "1022",
				PackagePort: "8888",
			},
		},
		// Test case for unmarshalling being changed to be string
		// values for numbers
		{
			jsonString: `{
				  "test-device1": {
					"bucket": "",
					"device-ip": "localhost",
					"device-name": "test-device1",
					"image": "",
					"package-port": "8888",
					"package-repo": "",
					"ssh-port": "1022"
				  }
				}
				`,
			deviceName: "test-device1",
			deviceConfig: DeviceConfig{
				DeviceName:  "test-device1",
				DeviceIP:    "localhost",
				PackagePort: "8888",
				SSHPort:     "1022",
			},
		},
	}

	for i, test := range tests {
		var data map[string]interface{}
		err := json.Unmarshal([]byte(test.jsonString), &data)
		if err != nil {
			t.Errorf("Error parsing json for %v: %v", i, err)
		}

		actualDevice, ok := mapToDeviceConfig(data[test.deviceName])
		if !ok {
			t.Errorf("Error mapping to DeviceConfig %v: %v", i, data)
		}
		expected := test.deviceConfig
		if actualDevice != expected {
			t.Errorf("Test %v: unexpected deviceConfig: %v. Expected %v", i, actualDevice, expected)
		}
	}

}

func helperCommandForInitEnv(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)

	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "ALLOW_ENV=1")
	// Pass file so when it is checked, it exists.
	if tempGlobalSettingsFile != "" {
		cmd.Env = append(cmd.Env, fmt.Sprintf("GLOBAL_SETTINGS_FILE=%v", tempGlobalSettingsFile))
	}

	return cmd
}

func helperCommandForInitEnvNoExistingFile(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)

	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "ALLOW_ENV=1")
	cmd.Env = append(cmd.Env, "ALLOW_SET=1")
	cmd.Env = append(cmd.Env, fmt.Sprintf("GLOBAL_SETTINGS_FILE=%v", "/file/does/not/exist.json"))

	return cmd
}

func helperCommandForGetFuchsiaProperty(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

func helperCommandForNoDefaultDevice(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "NO_DEFAULT_DEVICE=1")

	return cmd
}
func helperCommandForSetTesting(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "ALLOW_SET=1")
	return cmd
}
func helperCommandForRemoveTesting(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "ALLOW_SET=1")
	cmd.Env = append(cmd.Env, "ALLOW_REMOVE=1")
	return cmd
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
	case "ffx":
		fakeFfxTarget(args)
	case "ssh":
		fakeSSH(args)
	case "sftp":
		fakeSFTP(args, os.Stdin)
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
		case "gs://fuchsia/development/test-version/images*":
			expected = []string{args[0], args[1]}
			fmt.Printf("%v/image1.tgz\n", args[1])
			fmt.Printf("%v/image2.tgz\n", args[1])
		case "gs://private-bucket/development/test-version/images*":
			expected = []string{args[0], args[1]}
			fmt.Printf("%v/priv-image3.tgz\n", args[1])
			fmt.Printf("%v/priv-image4.tgz\n", args[1])
		case "gs://fuchsia/development/multi-version**/images*":
			expected = []string{args[0], args[1]}
			fmt.Print("gs://fuchsia/development/multi-version1/images/image1.tgz\n")
			fmt.Print("gs://fuchsia/development/multi-version2/images/image1.tgz\n")
		case "gs://new/development/multi-version**/images*":
			expected = []string{args[0], args[1]}
			fmt.Print("gs://new/development/multi-version1/images/priv-image1.tgz\n")
			fmt.Print("gs://new/development/multi-version2/images/priv-image1.tgz\n")
		case "gs://fuchsia/development/unknown/images*":
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
	expectedResolveArgs := []string{"resolve", "-device-limit", "1", "-ipv4=false", "test-device-df"}
	expectedListArgs := []string{"list", "--full", "-ipv4=false"}
	if args[0] == "resolve" {
		expected = expectedResolveArgs
		if args[len(args)-1] == "test-device-df" {
			fmt.Println(resolvedAddr)
		} else {
			fmt.Fprintf(os.Stderr, "resolve.go:76: no devices found for domains: [%v]", args[len(args)-1])
			os.Exit(2)
		}
	} else if args[0] == "list" {
		expected = expectedListArgs
		fmt.Printf(`123-123-123-1df test-device-df
		456-456-456-4df another-test-device-df`)
	} else {
		fmt.Fprintf(os.Stderr, "unexpected argument to device finder: %v", args[0])
		os.Exit(1)
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

func fakeFfxTarget(args []string) {
	expected := []string{}
	expectedResolveArgs := []string{"target", "list", "--format", "a"}
	expectedListFull := []string{"target", "list", "--format", "s"}
	if args[0] == "target" && args[3] == "a" {
		expected = expectedResolveArgs
		if args[len(args)-1] == "test-device" {
			fmt.Println(resolvedAddr)
		} else {
			fmt.Fprintf(os.Stderr, "No devices found.")
			os.Exit(2)
		}
	} else if args[0] == "target" && args[3] == "s" {
		expected = expectedListFull
		fmt.Printf(`123-123-123-123 test-device
		456-456-456-456 another-test-device`)
	}
	expectedResolveArgs = append(expectedResolveArgs, args[len(args)-1])
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

func TestFakeFfx(*testing.T) {
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
	if strings.HasSuffix(args[0], "device-finder") {
		fakeDeviceFinder(args[1:])
		os.Exit(0)
	}
	if strings.HasSuffix(args[0], "ffx") && args[1] == "doctor" {
		fmt.Printf("Welcome to ffx doctor.")
		os.Exit(0)
	}
	if strings.HasSuffix(args[0], "ffx") && args[1] == "target" {
		fakeFfxTarget(args[1:])
		os.Exit(0)
	}
	if args[1] != "config" {
		fmt.Fprintf(os.Stderr, "Unexpected command %v, expected `config`", args[1])
		os.Exit(2)
	}
	switch args[2] {
	case "env":
		if os.Getenv("ALLOW_ENV") != "1" {
			fmt.Fprintf(os.Stderr, "Verb `env` not allowed")
			os.Exit(2)
		}
		handleEnvFake(args[3:])
	case "get":
		if !handleGetFake(args[3:]) {
			fmt.Fprintf(os.Stderr, "Whatever error message")
			os.Exit(2)
		}
	case "set":
		if os.Getenv("ALLOW_SET") != "1" {
			fmt.Fprintf(os.Stderr, "Verb `set` not allowed")
			os.Exit(2)
		}
		handleSetFake(args[3:])
	case "remove":
		handleRemoveFake(args[3:])
	default:
		fmt.Fprintf(os.Stderr, "Unexpected verb %v", args[2])
		os.Exit(2)
	}
}

func handleEnvFake(args []string) {
	if len(args) == 0 {
		fmt.Printf("\nEnvironment:\n\tUser: /home/someuser/some/path/.ffx_user_config.json\n\tBuild:  none\n\tGlobal: %v\n", os.Getenv("GLOBAL_SETTINGS_FILE"))
	} else if args[0] == "set" {
		if len(args) != 4 {
			fmt.Fprintf(os.Stderr, "env set expects 3 args, got %v", args[1:])
			os.Exit(2)
		}
		if len(args[1]) <= 0 {
			fmt.Fprintf(os.Stderr, "env set requires a filename: %v", args[1:])
			os.Exit(2)
		}
		if args[2] != "--level" || args[3] != "global" {
			fmt.Fprintf(os.Stderr, "env set should only set global level %v", args[1:])
			os.Exit(2)
		}
	} else {
		fmt.Fprintf(os.Stderr, "Unexpected env %v", args)
		os.Exit(2)
	}
}

func handleGetFake(args []string) bool {
	var (
		dataName   string
		deviceData map[string]interface{}
	)

	deviceName := os.Getenv("TEST_DEFAULT_DEVICE_NAME")
	if deviceName == "" {
		deviceName = "fake-target-device-name"
	}
	currentDeviceData := os.Getenv("TEST_CURRENT_DEVICE_DATA")
	if len(currentDeviceData) > 0 {
		err := json.Unmarshal([]byte(currentDeviceData), &deviceData)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error parsing current data %v: %s", err, currentDeviceData)
			os.Exit(1)
		}
	} else {
		deviceData = make(map[string]interface{})
	}
	if value, ok := deviceData["device-name"].(string); ok {
		dataName = value
	}

	switch args[0] {
	case "DeviceConfiguration._DEFAULT_DEVICE_":
		if os.Getenv("NO_DEFAULT_DEVICE") != "1" {
			fmt.Printf("\"%v\"\n", deviceName)
		} else {
			return false
		}
	case fmt.Sprintf("DeviceConfiguration.%v.device-name", deviceName):
		fmt.Println(deviceName)
	case "DeviceConfiguration":
		fmt.Println(`{
			"_DEFAULT_DEVICE_":"atom-slaw-cozy-rigor",
			"fake-target-device-name":{
				"bucket":"fuchsia-bucket","device-ip":"","device-name":"fake-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
			},
			"another-target-device-name":{
				"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
			}
			}`)
	case "DeviceConfiguration.another-target-device-name":
		fmt.Println(`{
				"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
			}`)
	case "DeviceConfiguration.fake-target-device-name":
		fmt.Println(`{
				"bucket":"","device-ip":"","device-name":"fake-target-device-name","image":"","package-port":"","package-repo":"","ssh-port":""
			}`)
	default:
		if args[0] == fmt.Sprintf("DeviceConfiguration.%s", dataName) {
			fmt.Println(currentDeviceData)

		} else {
			return false
		}
	}
	return true
}

func handleSetFake(args []string) {
	sdk := SDKProperties{}
	expectedData := os.Getenv("TEST_EXPECTED_SET_DATA")
	var data map[string]interface{}
	if len(expectedData) > 0 {
		err := json.Unmarshal([]byte(expectedData), &data)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error parsing configuration data %v: %s", err, expectedData)
			os.Exit(1)
		}
	} else {
		data = make(map[string]interface{})
	}

	// All sets should be at the global level
	if args[0] != "--level" || args[1] != "global" {
		fmt.Fprintf(os.Stderr, "set command should only be used at global level: %v", args)
		os.Exit(1)
	}
	if len(args) > 4 {
		fmt.Fprintf(os.Stderr, "Invalid number of arguments expected 4 got: %v", args)
		os.Exit(1)
	}
	// Check the property name
	parts := strings.Split(args[2], ".")
	switch len(parts) {
	case 3:
		// This is a device setting
		if parts[0] != "DeviceConfiguration" || parts[1] != "new-device-name" {
			fmt.Fprintf(os.Stderr, "Expected device property name format. Got: %v", parts)
			os.Exit(1)
		}
		if !sdk.IsValidProperty(parts[2]) {
			fmt.Fprintf(os.Stderr, "Invalid property name for a device: %v", parts)
			os.Exit(1)
		}

	case 2:
		// Setting a reserved property
		if parts[0] != "DeviceConfiguration" || !isReservedProperty(parts[1]) {
			fmt.Fprintf(os.Stderr, "Unexpected property being set: %v", parts)
			os.Exit(1)
		}
	default:
		fmt.Fprintf(os.Stderr, "Unexpected property being set: %v", parts)
		os.Exit(1)
	}
	if expectedValue, ok := data[args[2]].(string); ok {
		if expectedValue != args[3] {
			fmt.Fprintf(os.Stderr, "Unexpected property %v value being set: %v, expected %v", args[2], args[3], expectedValue)
			os.Exit(1)
		}
	} else {
		fmt.Fprintf(os.Stderr, "Unexpected property %v value attempted to be set", args[2])
		os.Exit(1)
	}
}

func handleRemoveFake(args []string) {
	// All removes should be at the global level
	if args[0] != "--level" || args[1] != "global" {
		fmt.Fprintf(os.Stderr, "remove command should only be used at global level: %v", args)
		os.Exit(1)
	}
	if len(args) > 4 {
		fmt.Fprintf(os.Stderr, "Invalid number of arguments expected 4 got: %v", args)
		os.Exit(1)
	}
	// Check the property name
	parts := strings.Split(args[2], ".")
	switch len(parts) {
	case 2:
		if parts[0] != "DeviceConfiguration" || parts[1] != "old-device-name" {
			fmt.Fprintf(os.Stderr, `BUG: An internal command error occurred.
			Config key not found`)
			os.Exit(1)
		}
	default:
		fmt.Fprintf(os.Stderr, "Unexpected property being removed: %v", parts)
		os.Exit(1)
	}
}

func fakeSFTP(args []string, stdin *os.File) {
	expected := []string{}
	sshConfigMatch := "/.*/sshconfig"
	if os.Getenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG") != "" {
		sshConfigMatch = "custom-sshconfig"
	}
	sshConfigArgs := []string{"-F", sshConfigMatch}
	expected = append(expected, sshConfigArgs...)

	targetaddr := "[fe80::c0ff:eee:fe00:4444%en0]"

	customSSHPortArgs := []string{"-p", "1022"}
	if os.Getenv("FSERVE_TEST_USE_CUSTOM_SSH_PORT") != "" {
		expected = append(expected, customSSHPortArgs...)
	}

	privateKeyArgs := []string{"-i", "private-key"}
	if os.Getenv("FSERVE_TEST_USE_PRIVATE_KEY") != "" {
		expected = append(expected, privateKeyArgs...)
	}

	expected = append(expected, "-q", "-b", "-", targetaddr)

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
		fmt.Fprintf(os.Stderr, "unexpected sftp args  %v expected %v", args, expected)
		os.Exit(1)
	}

	expectedSrc := "/some/src/file"
	expectedDst := "/dst/file"

	expectedSFTPInput := fmt.Sprintf("get %v %v", expectedSrc, expectedDst)

	if os.Getenv("SFTP_TO_TARGET") == "1" {
		expectedSFTPInput = fmt.Sprintf("put %v %v", expectedSrc, expectedDst)
	}

	inputToSFTP, err := ioutil.ReadAll(stdin)
	if err != nil {
		fmt.Fprintf(os.Stderr, "got error converting stdin to string: %v", err)
		os.Exit(1)
	}
	actualInputToSFTP := string(inputToSFTP)
	if actualInputToSFTP != expectedSFTPInput {
		fmt.Fprintf(os.Stderr, "expected %v, got %v", expectedSFTPInput, actualInputToSFTP)
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

	customSSHPortArgs := []string{"-p", "1022"}
	if os.Getenv("FSERVE_TEST_USE_CUSTOM_SSH_PORT") != "" {
		expectedHostConnection = append(expectedHostConnection, customSSHPortArgs...)
		expectedSetSource = append(expectedSetSource, customSSHPortArgs...)
		targetIndex = targetIndex + 2
	}

	if os.Getenv("FSERVE_TEST_USE_PRIVATE_KEY") != "" {
		targetIndex = targetIndex + 2
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
