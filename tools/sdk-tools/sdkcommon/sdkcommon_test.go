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

var defaultTargetJSONOutput = fmt.Sprintf(`[{
	"nodename":"test-device",
	"rcs_state":"Y",
	"serial":"<unknown>",
	"target_type":"Unknown",
	"target_state":"Product",
	"addresses":["%v"]
},{
	"nodename":"another-test-device",
	"rcs_state":"N",
	"serial":"<unknown>",
	"target_type":"Unknown",
	"target_state":"Product",
	"addresses":["fe80::9ded:df4f:5ee8:605f", "123-123-123"]
}]`, resolvedAddr)

const getSSHAddressOutput = "[fe80::9ded:df4f:5ee8:605f]:8022"

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
	defer clearTestEnv()
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
	defer clearTestEnv()
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

func TestListDevicesFfx(t *testing.T) {
	defer clearTestEnv()

	tests := []struct {
		ffxTargetListOutput          string
		ffxTargetGetSSHAddressOutput string
		expectedFuchsiaDevice        []*FuchsiaDevice
	}{
		{
			ffxTargetListOutput: `[{"nodename":"another-test-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["ac80::9ded:df4f:5ee8:605f", "fe80::9ded:df4f:5ee8:605f"]}]`,
			ffxTargetGetSSHAddressOutput: "[fe80::9ded:df4f:5ee8:605f]:8022",
			expectedFuchsiaDevice: []*FuchsiaDevice{
				{
					IpAddr: "fe80::9ded:df4f:5ee8:605f",
					Name:   "another-test-device",
				},
			},
		},
		{
			ffxTargetListOutput: `[{"nodename":"test-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown","target_state":"Product",
			"addresses":["127.0.0.1"]}]`,
			expectedFuchsiaDevice: []*FuchsiaDevice{
				{
					IpAddr: "127.0.0.1",
					Name:   "test-device",
				},
			},
		},
		{
			ffxTargetListOutput: `[{"nodename":"test-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["ac80::9ded:df4f:5ee8:605f"]},
			{"nodename":"another-test-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["ac80::9ded:df4f:5ee8:605f", "127.0.0.1"]}]`,
			ffxTargetGetSSHAddressOutput: "127.0.0.1:8022",
			expectedFuchsiaDevice: []*FuchsiaDevice{
				{
					IpAddr: "ac80::9ded:df4f:5ee8:605f",
					Name:   "test-device",
				},
				{
					IpAddr: "127.0.0.1",
					Name:   "another-test-device",
				},
			},
		},
	}
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
	for _, test := range tests {
		clearTestEnv()
		ExecCommand = helperCommandForSDKCommon
		os.Setenv("TEST_FFX_TARGET_LIST_OUTPUT", test.ffxTargetListOutput)
		os.Setenv("TEST_GET_SSH_ADDRESS_OUTPUT", test.ffxTargetGetSSHAddressOutput)
		output, err := testSDK.ListDevices()
		if err != nil {
			t.Fatalf("Unexpected error: %v", err)
		}
		if d := cmp.Diff(test.expectedFuchsiaDevice, output, cmp.Comparer(compareFuchsiaDevices)); d != "" {
			t.Fatalf("listDevices mismatch: (-want +got):\n%s", d)
		}
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
	}()
	defer clearTestEnv()
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
		if err := testSDK.RunSSHShell(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.verbose, test.args); err != nil {
			t.Errorf("TestRunSSHShell (using port) %d error: %v", i, err)
		}
		if _, err := testSDK.RunSSHCommand(targetAddress, test.customSSHConfig, test.privateKey, test.sshPort, test.verbose, test.args); err != nil {
			t.Errorf("TestRunSSHCommand %d error: %v", i, err)
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
		for _, option := range allSSHOptions {
			os.Setenv(option, "")
		}
	}()
	defer clearTestEnv()
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
	defer clearTestEnv()
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
	defer clearTestEnv()
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

	defer clearTestEnv()

	tests := []struct {
		ffxTargetDefaultGetOutput string
		skipCheckingFFX           bool
		expectedDefaultDevice     string
		execHelper                func(command string, s ...string) (cmd *exec.Cmd)
	}{
		{
			// fconfig has a default device.
			expectedDefaultDevice: "fake-target-device-name",
			execHelper:            helperCommandForGetFuchsiaProperty,
		},
		{
			expectedDefaultDevice: "",
			execHelper:            helperCommandForNoDefaultDevice,
		},
		{
			// ffx has a default device.
			ffxTargetDefaultGetOutput: "some-other-device",
			expectedDefaultDevice:     "some-other-device",
			execHelper:                helperCommandForNoDefaultDevice,
		},
		{
			// ffx and fconfig have two different default devices.
			ffxTargetDefaultGetOutput: "some-other-device",
			expectedDefaultDevice:     "fake-target-device-name",
			execHelper:                helperCommandForGetFuchsiaProperty,
		},
		{
			// ffx has a default device but we are skipping the check.
			ffxTargetDefaultGetOutput: "some-other-device",
			skipCheckingFFX:           true,
			expectedDefaultDevice:     "",
			execHelper:                helperCommandForNoDefaultDevice,
		},
	}
	for _, test := range tests {
		clearTestEnv()
		os.Setenv("TEST_FFX_TARGET_DEFAULT_GET", test.ffxTargetDefaultGetOutput)
		ExecCommand = test.execHelper
		val, err := sdk.getDefaultDeviceName(test.skipCheckingFFX)
		if err != nil {
			t.Fatalf("Unexpected err: %v", err)
		}
		if val != test.expectedDefaultDevice {
			t.Fatalf("Unexpected default device name. Got: %v, expected: %v", val, test.expectedDefaultDevice)
		}
	}
}

func TestGetFuchsiaProperty(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = helperCommandForGetFuchsiaProperty
	defer clearTestEnv()

	testData := []struct {
		device, property, expected, errString string
	}{
		{"", "device-name", "fake-target-device-name", ""},
		{"some-other-device", "device-name", "some-other-device", ""},
		{"some-other-device", "random-property", "", "Could not find property some-other-device.random-property"},
		{"", "random-property", "", "Could not find property fake-target-device-name.random-property"},
		{"", PackageRepoKey, "fake-target-device-name/packages/amber-files", ""},
		{"another-target-device-name", PackageRepoKey, "another-target-device-name/packages/amber-files", ""},
		{"unknown-device-name", PackageRepoKey, "unknown-device-name/packages/amber-files", ""},
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
	defer clearTestEnv()

	tests := []struct {
		ffxTargetListOutput string
		expectedLen         int
	}{
		{
			expectedLen: 4,
		},
		{
			ffxTargetListOutput: "[]",
			expectedLen:         2,
		},
	}

	for _, test := range tests {
		os.Setenv("TEST_FFX_TARGET_LIST_OUTPUT", test.ffxTargetListOutput)
		val, err := sdk.GetDeviceConfigurations()
		if err != nil {
			t.Errorf("unexpected err %v", err)
		}
		if len(val) != test.expectedLen {
			t.Errorf("TestGetDeviceConfigurations got length: %v, expected %v", len(val), test.expectedLen)
		}
	}
}

func TestGetDeviceConfiguration(t *testing.T) {
	sdk := SDKProperties{}
	ExecCommand = helperCommandForGetFuchsiaProperty
	defer clearTestEnv()

	tests := []struct {
		deviceName           string
		expectedDeviceConfig DeviceConfig
	}{
		{
			deviceName: "another-target-device-name",
			expectedDeviceConfig: DeviceConfig{
				DeviceName:  "another-target-device-name",
				Bucket:      "fuchsia-bucket",
				Image:       "release",
				SSHPort:     "22",
				PackageRepo: "another-target-device-name/packages/amber-files",
				PackagePort: "8083",
			},
		},
		{
			deviceName: "unknown-device",
			expectedDeviceConfig: DeviceConfig{
				DeviceName:  "unknown-device",
				Bucket:      "fuchsia",
				Image:       "",
				SSHPort:     "22",
				PackageRepo: "unknown-device/packages/amber-files",
				PackagePort: "8083",
			},
		},
	}
	for _, test := range tests {
		deviceConfig, err := sdk.GetDeviceConfiguration(test.deviceName)
		if err != nil {
			t.Fatalf("unexpected err %v", err)
		}
		if deviceConfig != test.expectedDeviceConfig {
			t.Fatalf("TestGetDeviceConfiguration failed. Wanted configuration %v: got %v", test.expectedDeviceConfig, deviceConfig)
		}
	}
}

func TestSaveDeviceConfiguration(t *testing.T) {
	sdk := SDKProperties{}
	defer clearTestEnv()

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
		{
			currentDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "1.1.1.1",
				Image:       "image-name",
				Bucket:      "buck-name",
				PackagePort: "8000",
				PackageRepo: "/usr/some/new/device",
				SSHPort:     "8022",
			},
			newDevice: DeviceConfig{
				DeviceName:  "new-device-name",
				DeviceIP:    "",
				Image:       "custom-image",
				Bucket:      "",
				PackagePort: "8000",
				PackageRepo: "/usr/some/new/device",
				SSHPort:     "8022",
			},
			expectedValues: map[string]string{
				"DeviceConfiguration.new-device-name.device-name":  "new-device-name",
				"DeviceConfiguration.new-device-name.bucket":       "",
				"DeviceConfiguration.new-device-name.device-ip":    "",
				"DeviceConfiguration.new-device-name.image":        "custom-image",
				"DeviceConfiguration.new-device-name.package-repo": "/usr/some/new/device",
			},
		},
	}
	for i, test := range tests {
		clearTestEnv()
		ExecCommand = helperCommandForSetTesting
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
	defer clearTestEnv()

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
	defer clearTestEnv()

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
	defer clearTestEnv()

	tests := []struct {
		defaultDeviceName            string
		deviceIP                     string
		deviceName                   string
		ffxTargetListOutput          string
		ffxTargetDefaultGetOutput    string
		ffxTargetGetSSHAddressOutput string
		expectedConfig               DeviceConfig
		expectedError                string
		execHelper                   func(command string, s ...string) (cmd *exec.Cmd)
	}{
		{
			// IP address passed in.
			deviceIP: resolvedAddr,
			expectedConfig: DeviceConfig{
				Bucket:      "fuchsia",
				DeviceIP:    resolvedAddr,
				SSHPort:     "22",
				PackageRepo: "packages/amber-files",
				PackagePort: "8083",
			},
			execHelper: helperCommandForGetFuchsiaProperty,
		},
		{
			// Device name passed in.
			defaultDeviceName: "another-test-device",
			deviceName:        "test-device",
			expectedConfig: DeviceConfig{
				DeviceName:   "test-device",
				Bucket:       "fuchsia",
				DeviceIP:     resolvedAddr,
				SSHPort:      "22",
				PackageRepo:  "test-device/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    false,
				Discoverable: true,
			},
			execHelper: helperCommandForGetFuchsiaProperty,
		},
		{
			// Device name passed in but is not discoverable and is not set in fconfig.
			deviceName: "some-unknown-device",
			expectedError: `Cannot get target address for some-unknown-device.
Try running 'ffx target list --format s' and verify the name matches in 'fconfig get-all'.`,
			execHelper: helperCommandForGetFuchsiaProperty,
		},
		{
			// Gets the default device when there are multiple devices in ffx target list.
			defaultDeviceName: "another-target-device-name",
			ffxTargetListOutput: fmt.Sprintf(`[{"nodename":"another-target-device-name",
			"rcs_state":"Y",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["%v"]
		},{
			"nodename":"another-test-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["fe80::9ded:df4f:5ee8:605f", "123-123-123"]}]`, resolvedAddr),
			expectedConfig: DeviceConfig{
				DeviceName:   "another-target-device-name",
				Bucket:       "fuchsia-bucket",
				Image:        "release",
				DeviceIP:     resolvedAddr,
				SSHPort:      "22",
				PackageRepo:  "another-target-device-name/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    true,
				Discoverable: true,
			},
			execHelper: helperCommandForGetFuchsiaProperty,
		},
		{
			// Returns an error if the default device is set but undiscoverable and doesn't have an
			// IP in config, even if another device is discoverable.
			defaultDeviceName: "fake-target-device-name",
			ffxTargetListOutput: `[{"nodename":"another-target-device-name",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["ac80::9ded:df4f:5ee8:605f"]}]`,
			expectedError: `Cannot get target address for fake-target-device-name.
Try running 'ffx target list --format s' and verify the name matches in 'fconfig get-all'.`,
			execHelper: helperCommandForGetFuchsiaProperty,
		},
		{
			// Uses the correct device IP when device has multiple address in ffx target list.
			ffxTargetListOutput: `[{"nodename":"another-test-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["ac80::9ded:df4f:5ee8:605f", "127.0.0.1"]}]`,
			ffxTargetGetSSHAddressOutput: "127.0.0.1:123456",
			expectedConfig: DeviceConfig{
				DeviceName:   "another-test-device",
				Bucket:       "fuchsia-bucket",
				Image:        "release",
				DeviceIP:     "127.0.0.1",
				SSHPort:      "123456",
				PackageRepo:  "another-test-device/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    false,
				Discoverable: true,
			},
			execHelper: helperCommandForNoDefaultDevice,
		},
		{
			// Uses the device IP that was set by the user even if ffx outputs something else.
			ffxTargetListOutput: `[{"nodename":"another-test-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["ac80::9ded:df4f:5ee8:605f"]}]`,
			expectedConfig: DeviceConfig{
				DeviceName:   "another-test-device",
				Bucket:       "fuchsia-bucket",
				Image:        "release",
				DeviceIP:     "127.0.0.1",
				SSHPort:      "123456",
				PackageRepo:  "another-test-device/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    false,
				Discoverable: true,
			},
			execHelper: helperCommandForNoDefaultDevice,
		},
		{
			// Gets the discoverable device if there is only 1, even if fconfig has multiple non-default devices.
			ffxTargetListOutput: `[{"nodename":"some-unknown-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["fe80::9ded:df4f:5ee8:605f"]}]`,
			expectedConfig: DeviceConfig{
				DeviceName:   "some-unknown-device",
				Bucket:       "fuchsia",
				DeviceIP:     "fe80::9ded:df4f:5ee8:605f",
				SSHPort:      "22",
				PackageRepo:  "some-unknown-device/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    false,
				Discoverable: true,
			},
			execHelper: helperCommandForNoDefaultDevice,
		},
		{
			// No discoverable device and no default device.
			ffxTargetListOutput: "[]",
			expectedError:       fmt.Sprintf("No devices found. %v", helpfulTipMsg),
			execHelper:          helperCommandForNoDefaultDevice,
		},
		{
			// Multiple discoverable devices found.
			expectedError: fmt.Sprintf("Multiple devices found. %v", helpfulTipMsg),
			execHelper:    helperCommandForNoDefaultDevice,
		},
		{
			// Multiple discoverable devices found but ffx has a default device.
			ffxTargetDefaultGetOutput: "test-device",
			expectedConfig: DeviceConfig{
				DeviceName:   "test-device",
				Bucket:       "fuchsia",
				DeviceIP:     resolvedAddr,
				SSHPort:      "22",
				PackageRepo:  "test-device/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    true,
				Discoverable: true,
			},
			execHelper: helperCommandForNoDefaultDevice,
		},
		{
			// Multiple discoverable devices found, fconfig has a default remote device and ffx has a default device.
			defaultDeviceName:         "remote-target-name",
			ffxTargetDefaultGetOutput: "test-device",
			expectedConfig: DeviceConfig{
				DeviceName:   "remote-target-name",
				Bucket:       "fuchsia-bucket",
				Image:        "release",
				DeviceIP:     "::1",
				SSHPort:      "22",
				PackageRepo:  "remote-target-name/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    true,
				Discoverable: false,
			},
			execHelper: helperCommandForRemoteTarget,
		},
		{
			// Multiple discoverable devices found but ffx has a default device that isn't discoverable.
			ffxTargetDefaultGetOutput: "some-unknown-default-device",
			expectedError: `Cannot get target address for some-unknown-default-device.
Try running 'ffx target list --format s' and verify the name matches in 'fconfig get-all'.`,
			execHelper: helperCommandForNoDefaultDevice,
		},
		{
			// No discoverable device but fconfig has a remote default target.
			defaultDeviceName:   "remote-target-name",
			ffxTargetListOutput: "[]",
			expectedConfig: DeviceConfig{
				DeviceName:   "remote-target-name",
				Bucket:       "fuchsia-bucket",
				Image:        "release",
				DeviceIP:     "::1",
				SSHPort:      "22",
				PackageRepo:  "remote-target-name/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    true,
				Discoverable: false,
			},
			execHelper: helperCommandForRemoteTarget,
		},
		{
			// One discoverable device and fconfig has a remote default target.
			defaultDeviceName: "remote-target-name",
			ffxTargetListOutput: `[{"nodename":"target-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["123-123-123"]}]`,
			expectedConfig: DeviceConfig{
				DeviceName:   "remote-target-name",
				Bucket:       "fuchsia-bucket",
				Image:        "release",
				DeviceIP:     "::1",
				SSHPort:      "22",
				PackageRepo:  "remote-target-name/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    true,
				Discoverable: false,
			},
			execHelper: helperCommandForRemoteTarget,
		},
		{
			// One discoverable device and no configured device.
			ffxTargetListOutput: `[{"nodename":"target-device",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["123-123-123"]}]`,
			expectedConfig: DeviceConfig{
				DeviceName:   "target-device",
				Bucket:       "fuchsia",
				DeviceIP:     "123-123-123",
				SSHPort:      "22",
				PackageRepo:  "target-device/packages/amber-files",
				PackagePort:  "8083",
				IsDefault:    false,
				Discoverable: true,
			},
			execHelper: helperCommandForNoConfiguredDevices,
		},
		{
			// Multiple discoverable devices and no configured device.
			ffxTargetListOutput: `[{"nodename":"another-target-device-name",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["123-123-123"]
		},{
			"nodename":"remote-target-name",
			"rcs_state":"N",
			"serial":"<unknown>",
			"target_type":"Unknown",
			"target_state":"Product",
			"addresses":["::1"]}]`,
			expectedError: fmt.Sprintf("Multiple devices found. %v", helpfulTipMsg),
			execHelper:    helperCommandForNoConfiguredDevices,
		},
	}
	for i, test := range tests {
		clearTestEnv()
		os.Setenv("TEST_DEFAULT_DEVICE_NAME", test.defaultDeviceName)
		os.Setenv("TEST_FFX_TARGET_LIST_OUTPUT", test.ffxTargetListOutput)
		os.Setenv("TEST_FFX_TARGET_DEFAULT_GET", test.ffxTargetDefaultGetOutput)
		os.Setenv("TEST_GET_SSH_ADDRESS_OUTPUT", test.ffxTargetGetSSHAddressOutput)
		ExecCommand = test.execHelper

		config, err := sdk.ResolveTargetAddress(test.deviceIP, test.deviceName)
		if err != nil {
			message := fmt.Sprintf("%v", err)
			if message != test.expectedError {
				t.Fatalf("Error '%v' did not match expected error '%v'", message, test.expectedError)
			}
		} else if test.expectedError != "" {
			t.Fatalf("Expected error '%v', but got no error", test.expectedError)
		}
		if config != test.expectedConfig {
			t.Fatalf("test case %v: got config: '%v' did not match expected '%v'", i, config, test.expectedConfig)
		}
	}
}

func TestRunFFXDoctor(t *testing.T) {
	sdk := SDKProperties{}

	ExecCommand = helperCommandForSetTesting
	defer clearTestEnv()

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
	testSDK := SDKProperties{
		dataPath: t.TempDir(),
	}
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

		actualDevice, ok := testSDK.mapToDeviceConfig(data[test.deviceName])
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

	// Set this in the environment, so we can control the result.
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

	// Set this in the environment, so we can control the result.
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
	// Set this in the environment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

func helperCommandForNoDefaultDevice(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the environment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "NO_DEFAULT_DEVICE=1")

	return cmd
}

func helperCommandForRemoteTarget(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the environment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "FFX_TEST_REMOTE_TARGET_FCONFIG=1")
	return cmd
}

func helperCommandForNoConfiguredDevices(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the environment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "FFX_NO_CONFIGURED_DEVICES=1")
	cmd.Env = append(cmd.Env, "NO_DEFAULT_DEVICE=1")
	return cmd
}

func helperCommandForSetTesting(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the environment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	cmd.Env = append(cmd.Env, "ALLOW_SET=1")
	return cmd
}
func helperCommandForRemoveTesting(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFfx", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the environment, so we can control the result.
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

func fakeFfxTarget(args []string) {
	expected := []string{}
	expectedListArgs := []string{"target", "list", "--format", "json"}
	expectedGetSSHAddressArgs := []string{"--target", "another-test-device", "target", "get-ssh-address"}
	expectedGetDefaultTarget := []string{"target", "default", "get"}
	if args[0] == "target" && args[1] == "list" {
		expected = expectedListArgs
		if os.Getenv("TEST_FFX_TARGET_LIST_OUTPUT") != "" {
			fmt.Println(os.Getenv("TEST_FFX_TARGET_LIST_OUTPUT"))
		} else {
			fmt.Println(defaultTargetJSONOutput)
		}
	} else if args[0] == "--target" && args[len(args)-1] == "get-ssh-address" {
		expected = expectedGetSSHAddressArgs
		if os.Getenv("TEST_GET_SSH_ADDRESS_OUTPUT") != "" {
			fmt.Println(os.Getenv("TEST_GET_SSH_ADDRESS_OUTPUT"))
		} else {
			fmt.Println(getSSHAddressOutput)
		}
	} else if args[0] == "target" && args[1] == "default" {
		expected = expectedGetDefaultTarget
		if os.Getenv("TEST_FFX_TARGET_DEFAULT_GET") != "" {
			fmt.Println(os.Getenv("TEST_FFX_TARGET_DEFAULT_GET"))
		} else {
			fmt.Println("")
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
	if strings.HasSuffix(args[0], "ffx") && args[1] == "doctor" {
		fmt.Printf("Welcome to ffx doctor.")
		os.Exit(0)
	}
	if strings.HasSuffix(args[0], "ffx") && (args[1] == "target" || args[1] == "--target") {
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
		if os.Getenv("FFX_TEST_REMOTE_TARGET_FCONFIG") == "1" {
			fmt.Println(`{
				"_DEFAULT_DEVICE_":"remote-target-name",
				"remote-target-name":{
					"bucket":"fuchsia-bucket","device-ip":"::1","device-name":"remote-target-name","image":"release","package-port":"","package-repo":"","ssh-port":"22", "default": "true"
				},
				"another-target-device-name":{
					"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
				}
				}`)
		} else if os.Getenv("NO_DEFAULT_DEVICE") == "1" {
			fmt.Println(`{
				"_DEFAULT_DEVICE_":"",
				"fake-target-device-name":{
					"bucket":"fuchsia-bucket","device-ip":"","device-name":"fake-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
				},
				"another-target-device-name":{
					"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22", "default": "false"
				},
				"another-test-device":{
					"bucket":"fuchsia-bucket","device-ip":"127.0.0.1","device-name":"another-test-device","image":"release","package-port":"","package-repo":"","ssh-port":"123456", "default": "false"
				}
				}`)
		} else if os.Getenv("FFX_NO_CONFIGURED_DEVICES") == "1" {
			fmt.Println("{}")
		} else {
			fmt.Println(`{
					"_DEFAULT_DEVICE_":"another-target-device-name",
					"fake-target-device-name":{
						"bucket":"fuchsia-bucket","device-ip":"","device-name":"fake-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
					},
					"another-target-device-name":{
						"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22", "default": "true"
					}
					}`)
		}
	case "DeviceConfiguration.another-target-device-name":
		fmt.Println(`{
				"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22", "default": "true"
			}`)
	case "DeviceConfiguration.fake-target-device-name":
		fmt.Println(`{
				"bucket":"","device-ip":"","device-name":"fake-target-device-name","image":"","package-port":"","package-repo":"","ssh-port":""
			}`)
	case "DeviceConfiguration.remote-target-name":
		fmt.Println(`{
					"bucket":"","device-ip":"::1","device-name":"remote-target-name","image":"","package-port":"","package-repo":"","ssh-port":""
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

func clearTestEnv() {
	ExecCommand = exec.Command

	os.Unsetenv("TEST_DEFAULT_DEVICE_NAME")
	os.Unsetenv("TEST_FFX_TARGET_LIST_OUTPUT")
	os.Unsetenv("TEST_FFX_TARGET_DEFAULT_GET")
	os.Unsetenv("TEST_GET_SSH_ADDRESS_OUTPUT")

	GetUserHomeDir = DefaultGetUserHomeDir
	GetUsername = DefaultGetUsername
	GetHostname = DefaultGetHostname

	os.Unsetenv("TEST_EXPECTED_SET_DATA")
	os.Unsetenv("TEST_CURRENT_DEVICE_DATA")
}
