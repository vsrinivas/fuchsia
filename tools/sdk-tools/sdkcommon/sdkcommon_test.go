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
	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, false, args); err != nil {
		t.Fatal(err)
	}

	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, false, args); err != nil {
		t.Fatal(err)
	}

	customSSHConfig = "custom-sshconfig"
	os.Setenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG", "1")
	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, false, args); err != nil {
		t.Fatal(err)
	}

	customSSHConfig = ""
	privateKey = "private-key"
	os.Setenv("FSERVE_TEST_USE_CUSTOM_SSH_CONFIG", "")
	os.Setenv("FSERVE_TEST_USE_PRIVATE_KEY", "1")
	if _, err := testSDK.RunSSHCommand(targetAddress, customSSHConfig, privateKey, false, args); err != nil {
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
		{"some-other-device", "random-property", "", ""},
		{"", "random-property", "", ""},
	}

	for _, data := range testData {
		val, err := sdk.GetFuchsiaProperty(data.device, data.property)
		if err != nil {
			if data.errString == "" {
				t.Fatalf("Unexpected error getting property %s.%s: %v", data.device, data.property, err)
			} else if !strings.Contains(fmt.Sprintf("%v", err), data.errString) {
				t.Fatalf("Expected error message %v not found in error %v", data.errString, err)
			}
			t.Fatalf("unexpected err %v", err)
		}
		if val != data.expected {
			t.Fatalf("GetFuchsiaProperyFailed %s.%s = %s, expected %s", data.device, data.property, val, data.expected)
		}
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

	newDevice := DeviceConfig{
		DeviceName:  "new-device-name",
		DeviceIP:    "1.1.1.1",
		Image:       "image-name",
		Bucket:      "buck-name",
		PackagePort: "8000",
		PackageRepo: "new/device/repo",
		SSHPort:     "22",
	}
	err := sdk.SaveDeviceConfiguration(newDevice)
	if err != nil {
		t.Fatalf("unexpected err %v", err)
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
		handleGetFake(args[3:])
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

func handleGetFake(args []string) {
	switch args[0] {
	case "DeviceConfiguration._DEFAULT_DEVICE_":
		if os.Getenv("NO_DEFAULT_DEVICE") != "1" {
			fmt.Println("DeviceConfiguration._DEFAULT_DEVICE_: \"fake-target-device-name\"")
		} else {
			fmt.Println("DeviceConfiguration._DEFAULT_DEVICE_: none")
		}
	case "DeviceConfiguration.fake-target-device-name.device-name":
		fmt.Printf("DeviceConfiguration.fake-target-device-name.device-name: \"fake-target-device-name\"\n")
	case "DeviceConfiguration":
		fmt.Printf(`DeviceConfiguration: {
			"_DEFAULT_DEVICE_":"atom-slaw-cozy-rigor",
			"fake-target-device-name":{
				"bucket":"fuchsia-bucket","device-ip":"","device-name":"fake-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
			},
			"another-target-device-name":{
				"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
			}
			}`)
	case "DeviceConfiguration.another-target-device-name":
		fmt.Println(`DeviceConfiguration.another-target-device-name:{
				"bucket":"fuchsia-bucket","device-ip":"","device-name":"another-target-device-name","image":"release","package-port":"","package-repo":"","ssh-port":"22"
			}`)
	default:
		fmt.Printf("%v: none\n", args[0])
	}
}

func handleSetFake(args []string) {
	sdk := SDKProperties{}
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
