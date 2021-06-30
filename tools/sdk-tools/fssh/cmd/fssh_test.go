// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"errors"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

type testSDKProperties struct {
	err                   error
	expectedTargetAddress string
}

func (testSDK testSDKProperties) RunSSHShell(targetAddress string, sshConfig string,
	privateKey string, sshPort string, verbose bool, sshArgs []string) error {
	if targetAddress == "" {
		return errors.New("target address must be specified")
	}
	if targetAddress != testSDK.expectedTargetAddress {
		return fmt.Errorf("target address %v did not match expected %v", targetAddress, testSDK.expectedTargetAddress)
	}
	return testSDK.err
}

const defaultIPAddress = "e80::c00f:f0f0:eeee:cccc"

func TestMain(t *testing.T) {
	dataDir := t.TempDir()
	savedArgs := os.Args
	savedCommandLine := flag.CommandLine
	sdkcommon.ExecCommand = helperCommandForFSSH
	defer func() {
		sdkcommon.ExecCommand = exec.Command
		os.Args = savedArgs
		flag.CommandLine = savedCommandLine
	}()

	tests := []struct {
		args                []string
		deviceConfiguration string
		defaultConfigDevice string
		ffxDefaultDevice    string
		ffxTargetList       string
		ffxTargetDefault    string
		expectedIPAddress   string
		expectedPort        string
		expectedSSHConfig   string
		expectedPrivateKey  string
		expectedSSHArgs     string
	}{
		// Test case for no configuration, but 1 device discoverable.
		{
			args:               []string{os.Args[0], "-data-path", dataDir},
			expectedIPAddress:  "::1f",
			expectedPort:       "",
			expectedSSHArgs:    "",
			expectedSSHConfig:  filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey: "",
			ffxTargetList:      `[{"nodename":"<unknown>","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
		},
		// Test case for --device-name ,  2 device discoverable.
		{
			args:               []string{os.Args[0], "-data-path", dataDir, "-level", "debug", "--device-name", "test-device"},
			expectedIPAddress:  "::1f",
			expectedPort:       "",
			expectedSSHArgs:    "",
			expectedSSHConfig:  filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey: "",
			ffxTargetList: `[{"nodename":"test-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]},
			{"nodename":"<unknown>","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::2f"]}]`,
		},
		// Test case fconfig default device.
		{
			args: []string{os.Args[0], "-data-path", dataDir},
			deviceConfiguration: `{ "_DEFAULT_DEVICE_":"remote-target-name",
		"remote-target-name":{
			"bucket":"fuchsia-bucket",
			"device-ip":"::1f",
			"device-name":"remote-target-name",
			"image":"release",
			"package-port":"",
			"package-repo":"",
			"ssh-port":"2202",
			"default": "true"}
		}`,
			defaultConfigDevice: "\"remote-target-name\"",
			expectedIPAddress:   "::1f",
			expectedPort:        "2202",
			expectedSSHArgs:     "",
			expectedSSHConfig:   filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey:  "",
		},
		// Test case fconfig non-default device with --device-name
		{
			args: []string{os.Args[0], "-data-path", dataDir, "--device-name", "test-target-name"},
			deviceConfiguration: `{ "_DEFAULT_DEVICE_":"remote-target-name",
				"remote-target-name":{
					"bucket":"fuchsia-bucket",
					"device-ip":"::1f",
					"device-name":"remote-target-name",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"ssh-port":"2202",
					"default": "true"},
					"test-target-name":{
						"bucket":"fuchsia-bucket",
						"device-ip":"::ff",
						"device-name":"test-target-name",
						"image":"release",
						"package-port":"",
						"package-repo":"",
						"ssh-port":"",
						"default": "true"}
				}`,
			defaultConfigDevice: "\"remote-target-name\"",
			expectedIPAddress:   "::ff",
			expectedPort:        "",
			expectedSSHArgs:     "",
			expectedSSHConfig:   filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey:  "",
		},
	}

	for testcase, test := range tests {
		t.Run(fmt.Sprintf("testcase_%d", testcase), func(t *testing.T) {
			os.Args = test.args
			os.Setenv("_EXPECTED_IPADDR", test.expectedIPAddress)
			os.Setenv("_EXPECTED_PORT", test.expectedPort)
			os.Setenv("_EXPECTED_ARGS", test.expectedSSHArgs)
			os.Setenv("_EXPECTED_SSHCONFIG", test.expectedSSHConfig)
			os.Setenv("_EXPECTED_PRIVKEY", test.expectedPrivateKey)
			os.Setenv("_FAKE_FFX_DEVICE_CONFIG_DATA", test.deviceConfiguration)
			os.Setenv("_FAKE_FFX_DEVICE_CONFIG_DEFAULT_DEVICE", test.defaultConfigDevice)
			os.Setenv("_FAKE_FFX_TARGET_DEFAULT", test.ffxTargetDefault)
			os.Setenv("_FAKE_FFX_TARGET_LIST", test.ffxTargetList)
			flag.CommandLine = flag.NewFlagSet(os.Args[0], flag.ExitOnError)
			osExit = func(code int) {
				if code != 0 {
					t.Fatalf("Non-zero error code %d", code)
				}
			}
			main()
		})
	}
}

func TestSSH(t *testing.T) {

	defaultDeviceProperties := make(map[string]string)
	defaultDeviceProperties[fmt.Sprintf("test-device.%v", sdkcommon.DeviceIPKey)] = defaultIPAddress
	tests := []struct {
		sdk           sdkProvider
		verbose       bool
		targetAddress string
		sshConfig     string
		privateKey    string
		args          []string
		expectedError string
	}{
		{
			sdk:           testSDKProperties{},
			expectedError: "target address must be specified",
		},
		{
			sdk:           testSDKProperties{expectedTargetAddress: defaultIPAddress},
			targetAddress: defaultIPAddress,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
			},
			targetAddress: defaultIPAddress,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
			},
			targetAddress: defaultIPAddress,
			verbose:       true,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
			},
			targetAddress: defaultIPAddress,
			sshConfig:     "custom-config",
			privateKey:    "private-key",
			expectedError: "",
		},
	}

	for _, test := range tests {
		err := ssh(test.sdk, test.verbose, test.targetAddress, test.sshConfig, test.privateKey, "", test.args)
		if err != nil {
			message := fmt.Sprintf("%v", err)
			if message != test.expectedError {
				t.Fatalf("Unexpected error %v does not match %v", message, test.expectedError)
			}
		} else if test.expectedError != "" {
			t.Fatalf("Expected error '%v', but got no error", test.expectedError)
		}
	}
}

// See exec_test.go for details, but effectively this runs the function called TestHelperProcess passing
// the args.
func helperCommandForFSSH(command string, s ...string) (cmd *exec.Cmd) {

	var cs []string
	if strings.HasSuffix(command, "/ffx") {
		cs = []string{"-test.run=TestFakeFFX", "--"}
	} else if strings.HasSuffix(command, "/ssh") {
		cs = []string{"-test.run=TestFakeSSH", "--"}
	} else if strings.HasSuffix(command, "/ssh-keygen") {
		cs = []string{"-test.run=TestFakeKeygen", "--"}
	} else {
		return exec.Command(fmt.Sprintf("unsupported/command/%v", command))
	}

	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the environment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

func TestFakeKeygen(t *testing.T) {
	t.Helper()
	if os.Getenv("GO_WANT_HELPER_PROCESS") != "1" {
		return
	}
	defer os.Exit(0)
}
func TestFakeSSH(t *testing.T) {
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

	expectedIPAddress := os.Getenv("_EXPECTED_IPADDR")
	expectedPort := os.Getenv("_EXPECTED_PORT")
	expectedSSHArgs := os.Getenv("_EXPECTED_ARGS")
	expectedSSHConfig := os.Getenv("_EXPECTED_SSHCONFIG")
	//expectedPrivateKey := os.Getenv("_EXPECTED_PRIVKEY")
	expectedArgs := []string{
		args[0],
		"-F",
		expectedSSHConfig,
	}
	if expectedPort != "" {
		expectedArgs = append(expectedArgs, "-p", expectedPort)
	}

	expectedArgs = append(expectedArgs, expectedIPAddress)

	expectedArgs = append(expectedArgs, strings.Fields(expectedSSHArgs)...)

	if len(expectedArgs) != len(args) {
		fmt.Fprintf(os.Stderr, "Fake SSH expected %d args, but got %d args.\n", len(expectedArgs), len(args))
		fmt.Fprintf(os.Stderr, "Expected [%v]\nActual   [%v]\n", expectedArgs, args)
		os.Exit(1)
	}
	for i, arg := range args {
		if arg != expectedArgs[i] {
			fmt.Fprintf(os.Stderr, "Expected arg[%d] != actual arg. [%v] != [%v]", i, expectedArgs[i], arg)
			os.Exit(1)
		}
	}
	os.Exit(0)
}

func TestFakeFFX(t *testing.T) {
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
	if strings.HasSuffix(args[0], "ffx") && args[1] == "config" && args[2] == "env" {
		if len(args) == 3 {
			fmt.Printf("Welcome to ffx doctor.")
			fmt.Printf("Environment:\n")
			fmt.Printf("User: none\n")
			fmt.Printf("Build: none\n")
			fmt.Printf("Global: none\n")
			os.Exit(0)
		} else if args[3] == "set" {
			os.Exit(0)
		}
	} else if strings.HasSuffix(args[0], "ffx") && args[1] == "config" && args[2] == "get" {
		if len(args) > 3 {
			if args[3] == "DeviceConfiguration" {
				fmt.Printf(os.Getenv("_FAKE_FFX_DEVICE_CONFIG_DATA"))
				os.Exit(0)
			} else if args[3] == "DeviceConfiguration._DEFAULT_DEVICE_" {
				fmt.Printf(os.Getenv("_FAKE_FFX_DEVICE_CONFIG_DEFAULT_DEVICE"))
				os.Exit(0)
			}

		}
	} else if strings.HasSuffix(args[0], "ffx") && args[1] == "target" && args[2] == "default" && args[3] == "get" {
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_TARGET_DEFAULT"))
		os.Exit(0)
	} else if strings.HasSuffix(args[0], "ffx") && args[1] == "target" && args[2] == "list" && args[3] == "--format" && args[4] == "json" {
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_TARGET_LIST"))
		os.Exit(0)
	}

	fmt.Fprintf(os.Stderr, "Unknown command not supported in faking ffx %v", os.Args)
	os.Exit(2)

}
