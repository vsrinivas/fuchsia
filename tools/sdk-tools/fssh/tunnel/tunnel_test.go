// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package tunnel

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"testing"
)

func TestGenerateDefaultSSHConfig(t *testing.T) {
	var tests = []struct {
		template          string
		remote            string
		deviceIP          string
		tunnelPorts       []int
		sshControlPath    string
		verbose           bool
		expectedSSHConfig string
		expectedErrMsg    string
	}{
		{
			template:          "{{.Remote}} {{.DeviceIP}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			verbose:           false,
			expectedSSHConfig: "fake-remote-hostname fake-IP-address",
		},
		{
			template:          "{{.DeviceIP}}{{.DeviceIP}}{{.DeviceIP}}{{.DeviceIP}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			verbose:           false,
			expectedSSHConfig: "fake-IP-addressfake-IP-addressfake-IP-addressfake-IP-address",
		},
		{
			template:          "{{.Remote}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			verbose:           false,
			expectedSSHConfig: "fake-remote-hostname",
		},
		{
			template:          "{{.Remote}} {{.DeviceIP}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			verbose:           true,
			expectedSSHConfig: "fake-remote-hostname fake-IP-address" + DebugLoggingSSHConfig,
		},
		{
			template:          "{{.DeviceIP}}{{.DeviceIP}}{{.DeviceIP}}{{.DeviceIP}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			verbose:           true,
			expectedSSHConfig: "fake-IP-addressfake-IP-addressfake-IP-addressfake-IP-address" + DebugLoggingSSHConfig,
		},
		{
			template:          "{{.Remote}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			verbose:           true,
			expectedSSHConfig: "fake-remote-hostname" + DebugLoggingSSHConfig,
		},
		{
			template:          "{{.TunnelPath}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			sshControlPath:    "somthing",
			verbose:           false,
			expectedSSHConfig: sshControlPath,
		},
		{
			template:       "",
			remote:         "fake-remote-hostname",
			deviceIP:       "fake-IP-address",
			sshControlPath: "somthing",
			verbose:        false,
			expectedErrMsg: "empty SSH config generated",
		},
		{
			template:          "{{range .TunnelPorts}}{{.}},{{end}}",
			remote:            "fake-remote-hostname",
			deviceIP:          "fake-IP-address",
			tunnelPorts:       []int{1234, 8887, 8888, 9999},
			sshControlPath:    "somthing",
			verbose:           false,
			expectedSSHConfig: "1234,8887,9999,",
		},
		{
			template:       "{{.TunnelPath}}",
			remote:         "fake-remote-hostname",
			deviceIP:       "fake-IP-address",
			tunnelPorts:    []int{22, 8888, 21, 9999},
			sshControlPath: "somthing",
			verbose:        false,
			expectedErrMsg: "Cannot create SSH config with protected ports: 22, 21",
		},
	}
	for _, test := range tests {
		sshConfig, err := GenerateSSHConfig(test.template, test.remote, test.deviceIP, test.tunnelPorts, test.verbose)
		if err != nil {
			if err.Error() != test.expectedErrMsg {
				t.Errorf("Unexpected error calling GenerateDefaultSSHConfig: %s", err)
			}
		}
		if string(sshConfig) != test.expectedSSHConfig {
			t.Errorf("GenerateDefaultSSHConfig() got %s, want %s",
				string(sshConfig), test.expectedSSHConfig)
		}
	}

	tunnelPorts := []int{8888, 9013, 1234, 8083, 9865}
	expectedTunnelPorts := []int{9013, 1234, 9865, 8888}
	unexpectedTunnelPorts := []int{8083}
	realConfig, err := GenerateSSHConfig(DefaultSSHConfigTemplate, "fake-remote-hostname", "fake-IP-address", tunnelPorts, false)
	if err != nil {
		t.Errorf("Error calling GenerateDefaultSSHConfig: %s", err)
	}
	for _, port := range expectedTunnelPorts {
		if !strings.Contains(string(realConfig), fmt.Sprintf("RemoteForward %d [fake-IP-address]:%d", port, port)) {
			t.Errorf("Expected to find %d forwarded in the ssh config", port)
		}
	}
	for _, port := range unexpectedTunnelPorts {
		if strings.Contains(string(realConfig), fmt.Sprintf("RemoteForward %d [fake-IP-address]:%d", port, port)) {
			t.Errorf("Expected NOT to find %d forwarded as an extra port in the ssh config", port)
		}
	}
}

func TestStart(t *testing.T) {
	var tests = []struct {
		sshPath       string
		sshConfigPath string
		remote        string
		expectedArgs  []string
	}{
		{
			sshPath:       "fake/path/to/ssh",
			sshConfigPath: "fake/ssh/config/path",
			remote:        "fake-remote-hostname",
			expectedArgs: []string{
				"fake/path/to/ssh",
				"-F",
				"fake/ssh/config/path",
				"fake-remote-hostname",
				"-n",
				"echo",
				"Tunnel is established",
				"&&",
				"echo",
				ffxAddRemoteTargetMessage,
				"&&",
				"sleep",
				"infinity",
			},
		},
	}
	for _, test := range tests {
		cmd, err := Cmd(test.sshPath, test.sshConfigPath, test.remote)
		if err != nil {
			t.Errorf("Error calling Start: %s", err)
		}
		if cmd.Path != test.sshPath {
			t.Errorf("Got command path %s, want %s",
				cmd.Path, test.sshPath)
		}
		if len(cmd.Args) != len(test.expectedArgs) {
			t.Errorf("Got %d args, want %d args",
				len(test.expectedArgs), len(cmd.Args))
		}
		for i, expectedArg := range test.expectedArgs {
			if i > len(cmd.Args)-1 {
				t.Errorf("Expected arg %s but found no arg", expectedArg)
				continue
			}
			if cmd.Args[i] != expectedArg {
				t.Errorf("Got arg %s, want %s",
					cmd.Args[i], expectedArg)
			}
			if i == len(test.expectedArgs)-1 && len(cmd.Args) > len(test.expectedArgs) {
				t.Errorf("Extra unexpected args found: %s", strings.Join(cmd.Args[i+1:], ", "))
			}
		}
		if cmd.Path != test.sshPath {
			t.Errorf("Got command path %s, want %s",
				cmd.Path, test.sshPath)
		}
	}
}

// See exec_test.go for details, but effectively this runs the function called TestHelperProcess passing
// the args.
func helperCommandForTestTunnel(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeSSH", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the environment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

// TestFakeSSH isn't a real test. It's used as a helper process that
// mocks calling SSH.
func TestFakeSSH(*testing.T) {
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

	cmd, args := args[0], args[1:]
	switch cmd {
	case "/fake/ssh":
		remote := args[0]
		args := args[1:]
		if remote == "happy-exit" {
			handleHappyExit(args)
		} else if remote == "no-existing-ports" {
			handleNoExistingPorts(args)
		} else {
			fmt.Fprintf(os.Stderr, "Unknown mock host: %s", remote)
			os.Exit(1)
		}
	default:
		fmt.Fprintf(os.Stderr, "Unknown command %q\n", cmd)
		os.Exit(2)
	}
}

func handleHappyExit(args []string) {
	var expected []string
	switch args[0] {
	case "ps `pgrep -u $USER sshd` | grep notty":
		expected = []string{args[0]}
		fmt.Printf("1999999 ?        S      0:00 sshd: devuser@notty")
	case "ss -ln | grep :8022":
		fmt.Printf("tcp   LISTEN 0      128       [::]:8022               [::]:*")
		expected = []string{"ss -ln | grep :8022"}
	case "-O":
		expected = []string{"-O",
			"exit", "-S", sshControlPath}
	default:
		fmt.Fprintf(os.Stderr, "Unknown remote command: %s", args[0])
		os.Exit(2)
	}
	for i := range args {
		if args[i] != expected[i] {
			fmt.Fprintf(os.Stderr,
				"Mismatched args index %d. Got %s, expected %s",
				i, args[i], expected[i])
			fmt.Fprintf(os.Stderr, "Full args got %s, want %s",
				args, expected)
			os.Exit(3)
		}
	}
}

func handleNoExistingPorts(args []string) {
	var expected []string
	switch args[0] {
	case "ss -ln | grep :8022":
		// Return no text, but the rc = 1.
		os.Exit(1)
	case "-O":
		expected = []string{"-O",
			"exit", "-S", sshControlPath}
	default:
		fmt.Fprintf(os.Stderr, "Unknown remote command: %s",
			args[0])
		os.Exit(2)
	}
	for i := range args {
		if args[i] != expected[i] {
			fmt.Fprintf(os.Stderr,
				"Mismatched args index %d. Got %s, expected %s",
				i, args[i], expected[i])
			fmt.Fprintf(os.Stderr, "Full args got %s, expected %s",
				args, expected)
			os.Exit(3)
		}
	}
}

func TestCleanupTunnelHappyPath(t *testing.T) {
	ctx := context.Background()
	ExecCommand = helperCommandForTestTunnel
	defer func() { ExecCommand = exec.Command }()
	result, err := CleanupTunnel(ctx, "/fake/ssh", "happy-exit")
	if err != nil {
		t.Error(err)
	}
	foundPorts := "Existing port forwarding found on happy-exit"
	if !strings.Contains(result, foundPorts) {
		t.Errorf("Result does not contain %s. Found  %s", foundPorts, result)
	}
	cleanedRemotePorts := "Existing port forwarding found on happy-exit, Cleaning up sshd sessions remotely"
	if !strings.Contains(result, cleanedRemotePorts) {
		t.Errorf("Result does not contain %s. Found  %s", foundPorts, result)
	}
}

func TestCleanupTunnelNoExistingPorts(t *testing.T) {
	ctx := context.Background()
	ExecCommand = helperCommandForTestTunnel
	defer func() { ExecCommand = exec.Command }()
	result, err := CleanupTunnel(ctx, "/fake/ssh", "no-existing-ports")
	if err != nil {
		t.Error(err)
	}
	expected := ""
	if result != expected {
		t.Errorf("Unexpected string: %s. Found  %s", expected, result)
	}
}
