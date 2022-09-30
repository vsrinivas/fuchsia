// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"

	"github.com/google/subcommands"
)

func clearEnvVars() {
	os.Unsetenv("_EXPECTED_IPADDR")
	os.Unsetenv("_EXPECTED_PORT")
	os.Unsetenv("_EXPECTED_ARGS")
	os.Unsetenv("_EXPECTED_SSHCONFIG")
	os.Unsetenv("_EXPECTED_PRIVKEY")
	os.Unsetenv("_FAKE_FFX_DEVICE_CONFIG_DATA")
	os.Unsetenv("_FAKE_FFX_TARGET_DEFAULT")
	os.Unsetenv("_FAKE_FFX_TARGET_LIST")
	os.Unsetenv("_FAKE_FFX_GET_SSH_ADDRESS")
	os.Unsetenv("FFX_ISOLATE_DIR")
}

func TestMain(t *testing.T) {
	dataDir := t.TempDir()
	savedCommandLine := flag.CommandLine
	sdkcommon.ExecCommand = helperCommandForFSSH
	defer func() {
		clearEnvVars()
		sdkcommon.ExecCommand = exec.Command
		flag.CommandLine = savedCommandLine
	}()

	tests := []struct {
		cmd                    *fsshCmd
		name                   string
		args                   []string
		deviceConfiguration    string
		ffxTargetList          string
		ffxTargetDefault       string
		ffxTargetGetSSHAddress string
		expectedIPAddress      string
		expectedPort           string
		expectedSSHConfig      string
		expectedPrivateKey     string
		expectedSSHArgs        string
		expectedStatus         subcommands.ExitStatus
	}{
		{
			cmd:                    &fsshCmd{},
			name:                   "No configured device but 1 device is discoverable",
			args:                   []string{"-data-path", dataDir},
			expectedIPAddress:      "::1f",
			expectedPort:           "",
			expectedSSHArgs:        "",
			expectedSSHConfig:      filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey:     "",
			ffxTargetList:          `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:22`,
		},
		{
			cmd:                &fsshCmd{},
			name:               "Multiple discoverable devices and passing --device-name",
			args:               []string{"-data-path", dataDir, "-level", "debug", "--device-name", "test-device"},
			expectedIPAddress:  "::1f",
			expectedPort:       "",
			expectedSSHArgs:    "",
			expectedSSHConfig:  filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey: "",
			ffxTargetList: `[{"nodename":"test-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]},
			{"nodename":"random-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::2f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:22`,
		},
		{
			cmd:  &fsshCmd{},
			name: "ffx has a default device",
			args: []string{"-data-path", dataDir, "echo", "hello"},
			deviceConfiguration: `{
			"remote-target-name":{
				"bucket":"fuchsia-bucket",
				"device-name":"remote-target-name",
				"image":"release",
				"package-port":"",
				"package-repo":"",
				"default": "true"
			}
			}`,
			ffxTargetGetSSHAddress: `[::1f]:2202`,
			ffxTargetList: `[{"nodename":"remote-target-name","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]},
		{"nodename":"random-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::2f"]}]`,
			ffxTargetDefault:   "remote-target-name",
			expectedIPAddress:  "::1f",
			expectedPort:       "2202",
			expectedSSHArgs:    "echo hello",
			expectedSSHConfig:  filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey: "",
		},
		{
			cmd:  &fsshCmd{},
			name: "ffx non-default device with --device-name",
			args: []string{"-data-path", dataDir, "-device-name", "random-device", "-private-key", filepath.Join(dataDir, "pkey")},
			deviceConfiguration: `{
			"remote-target-name":{
				"bucket":"fuchsia-bucket",
				"device-name":"remote-target-name",
				"image":"release",
				"package-port":"",
				"package-repo":"",
				"default": "true"
			},
			"random-device":{
				"bucket":"fuchsia-bucket",
				"device-name":"test-target-name",
				"image":"release",
				"package-port":"",
				"package-repo":"",
				"default": "false"
			}
			}`,
			ffxTargetDefault: "remote-target-name",
			ffxTargetList: `[{"nodename":"remote-target-name","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]},
			{"nodename":"random-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::2f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:2202`,
			expectedIPAddress:      "::2f",
			expectedPort:           "8022",
			expectedSSHArgs:        "",
			expectedSSHConfig:      filepath.Join(dataDir, "sshconfig"),
			expectedPrivateKey:     filepath.Join(dataDir, "pkey"),
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			clearEnvVars()
			os.Setenv("_EXPECTED_IPADDR", test.expectedIPAddress)
			os.Setenv("_EXPECTED_PORT", test.expectedPort)
			os.Setenv("_EXPECTED_ARGS", test.expectedSSHArgs)
			os.Setenv("_EXPECTED_SSHCONFIG", test.expectedSSHConfig)
			os.Setenv("_EXPECTED_PRIVKEY", test.expectedPrivateKey)
			os.Setenv("_FAKE_FFX_DEVICE_CONFIG_DATA", test.deviceConfiguration)
			os.Setenv("_FAKE_FFX_TARGET_DEFAULT", test.ffxTargetDefault)
			os.Setenv("_FAKE_FFX_TARGET_LIST", test.ffxTargetList)
			os.Setenv("_FAKE_FFX_GET_SSH_ADDRESS", test.ffxTargetGetSSHAddress)
			os.Setenv("FFX_ISOLATE_DIR", t.TempDir())

			flagSet := flag.NewFlagSet("test", flag.ExitOnError)
			test.cmd.SetFlags(flagSet)

			if err := flagSet.Parse(test.args); err != nil {
				t.Fatalf("unexpected failure to parse args: %s", err)
			}

			ctx := context.Background()
			status := test.cmd.Execute(ctx, flagSet)
			if status != test.expectedStatus {
				t.Fatalf("Got status %v, want %v", status, test.expectedStatus)
			}
		})
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
	expectedPrivateKey := os.Getenv("_EXPECTED_PRIVKEY")
	expectedArgs := []string{
		args[0],
		"-F",
		expectedSSHConfig,
	}

	if expectedPrivateKey != "" {
		expectedArgs = append(expectedArgs, "-i", expectedPrivateKey)
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
	if strings.HasSuffix(args[0], "ffx") && args[1] == "config" && args[2] == "get" {
		if len(args) > 3 && (args[3] == "DeviceConfiguration" || args[3] == "device_config") {
			fmt.Printf(os.Getenv("_FAKE_FFX_DEVICE_CONFIG_DATA"))
			os.Exit(0)

		}
	} else if strings.HasSuffix(args[0], "ffx") && args[1] == "config" && (args[2] == "remove" || args[2] == "set") {
		os.Exit(0)
	} else if strings.HasSuffix(args[0], "ffx") && args[1] == "target" && args[2] == "default" && args[3] == "get" {
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_TARGET_DEFAULT"))
		os.Exit(0)
	} else if strings.HasSuffix(args[0], "ffx") && args[1] == "--machine" && args[2] == "json" && args[3] == "target" && args[4] == "list" {
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_TARGET_LIST"))
		os.Exit(0)
	} else if strings.HasSuffix(args[0], "ffx") && args[3] == "target" && args[4] == "get-ssh-address" {
		if args[2] == "random-device" {
			fmt.Printf("[::2f]:8022\n")
			os.Exit(0)
		}
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_GET_SSH_ADDRESS"))
		os.Exit(0)
	}

	fmt.Fprintf(os.Stderr, "Unknown command not supported in faking ffx %v", os.Args)
	os.Exit(2)

}
