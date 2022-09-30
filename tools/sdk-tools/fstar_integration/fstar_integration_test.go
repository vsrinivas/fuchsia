// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fstar_integration

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"os/user"
	"path/filepath"
	"strings"
	"syscall"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

type deviceInfo struct {
	Label string            `json:"label"`
	Child []deviceInfoChild `json:"child"`
}

type deviceInfoChild struct {
	Label string `json:"label"`
	// Use a interface because the value can be either bool, int, or string but will always be
	// a string when the label is set to "target", the only label we care about.
	Value interface{} `json:"value"`
}

const (
	targetLabel = "target"
	itemLabel   = "name"
)

type toolsPath struct {
	ffxPath     string
	ffxInstance *ffxutil.FFXInstance
	fsshPath    string
}

func verifyFFXTargetShowOutputWithDeviceName(output []byte, expectedDeviceName string) (bool, error) {
	var fullDeviceInfo []deviceInfo
	err := json.Unmarshal(output, &fullDeviceInfo)
	if err != nil {
		return false, err
	}
	for _, ele := range fullDeviceInfo {
		if ele.Label == targetLabel {
			for _, item := range ele.Child {
				if item.Label == itemLabel {
					return item.Value.(string) == expectedDeviceName, nil
				}
			}
		}
	}
	return false, fmt.Errorf("output json from 'ffx target show --json' is missing the target name label")
}

// findSubDir searches the root dir and returns the first path that matches dirName.
func findSubDir(root, dirName string) string {
	filePath := ""
	filepath.WalkDir(root,
		func(path string, info os.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if info.IsDir() && info.Name() == dirName {
				filePath = path
			}
			return nil
		})
	return filePath
}

// setUp sets up the environment, finds the paths for the host tools, and creates
// an isolated ffx instance.
func setUp(t *testing.T) (toolsPath, error) {
	var tools toolsPath
	ex, err := os.Executable()
	if err != nil {
		return tools, err
	}

	exDir := filepath.Dir(ex)
	runtimeDir := findSubDir(exDir, "fstar_runtime_deps")
	if len(runtimeDir) == 0 {
		return tools, fmt.Errorf("cannot find fstar_runtime_deps binary from %s", exDir)
	}

	hostToolsDir := filepath.Join(runtimeDir, "host_tools")
	if _, err := os.Stat(hostToolsDir); os.IsNotExist(err) {
		return tools, err
	}

	tools.ffxPath = filepath.Join(hostToolsDir, "ffx")
	if _, err := os.Stat(tools.ffxPath); os.IsNotExist(err) {
		return tools, fmt.Errorf("unable to find ffx: %w", err)
	}

	tools.fsshPath = filepath.Join(hostToolsDir, "fssh")
	if _, err := os.Stat(tools.fsshPath); os.IsNotExist(err) {
		return tools, fmt.Errorf("unable to find fssh: %w", err)
	}

	testOutDir := filepath.Join(t.TempDir(), "fssh_test")

	// Create a new isolated ffx instance.
	tools.ffxInstance, err = ffxutil.NewFFXInstance(context.Background(), tools.ffxPath, "", os.Environ(), os.Getenv(constants.NodenameEnvKey), os.Getenv(constants.SSHKeyEnvKey), testOutDir)
	if err != nil {
		return tools, fmt.Errorf("unable to create new ffx instance %w", err)
	}

	return tools, nil
}

func resetBuffers(stdout, stderr *bytes.Buffer) {
	stdout.Reset()
	stderr.Reset()
}

func TestFSSH(t *testing.T) {
	// In order to make sure the test cleans up the ffx instance, we capture SIGTERM until
	// we ensured the ffx instance is cleaned up. This is needed in a scenario where the test may exit
	// unexpectedly in infra (similar to pressing Ctrl-C when running this test locally).
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer cancel()

	tools, err := setUp(t)
	t.Cleanup(func() {
		if tools.ffxInstance != nil {
			if err := tools.ffxInstance.Stop(); err != nil {
				t.Logf("FFX didn't stop the running daemon %s", err)
			}
		}
	})

	if err != nil {
		t.Fatalf("unable to setup environment: %s", err)
	}

	deviceName := os.Getenv(constants.NodenameEnvKey)

	var stdoutBuf []byte
	stdout := bytes.NewBuffer(stdoutBuf)
	var stderrBuf []byte
	stderr := bytes.NewBuffer(stderrBuf)
	tools.ffxInstance.SetStdoutStderr(stdout, stderr)

	// Get the device IP.
	t.Logf("Getting the device IP")
	err = tools.ffxInstance.List(ctx, "--format", "a", deviceName)
	if err != nil {
		t.Fatalf("ffx target list returned unexpected error: %s", err)
	}

	ffxTargetListStderr := strings.TrimSpace(stderr.String())
	deviceIP := strings.TrimSpace(stdout.String())

	for _, envVar := range tools.ffxInstance.Env() {
		parts := strings.Split(envVar, "=")
		os.Setenv(parts[0], parts[1])
	}

	if ffxTargetListStderr != "" {
		t.Fatalf("ffx target list returned unexpected output to stderr: %s", ffxTargetListStderr)
	}

	if deviceIP == "" {
		t.Fatalf("device ip is empty")
	}

	t.Logf("Using device name: %s and device ip: %s", deviceName, deviceIP)

	// Set the default device in ffx.
	ffxSetDefaultDeviceArgs := []string{"target", "default", "set", deviceName}
	t.Logf("Setting the default device by running: %s %s", tools.ffxPath, ffxSetDefaultDeviceArgs)
	cmd := exec.Command(tools.ffxPath, ffxSetDefaultDeviceArgs...)

	_, err = cmd.Output()
	if err != nil {
		t.Errorf("ffx returned unexpected error: %s", err)
	}

	usr, err := user.Current()
	if err != nil {
		t.Fatalf("unable to get users home dir: %s", err)
	}

	expectedGetAllDeviceConfig := sdkcommon.DeviceConfig{
		DeviceName:   deviceName,
		Bucket:       "fuchsia",
		Image:        "",
		DeviceIP:     deviceIP,
		SSHPort:      "22",
		PackageRepo:  filepath.Join(usr.HomeDir, ".fuchsia", deviceName, "packages", "amber-files"),
		PackagePort:  "8083",
		IsDefault:    true,
		Discoverable: true,
	}

	// fssh into the default device.
	fsshArgs := []string{"-private-key", os.Getenv(constants.SSHKeyEnvKey), "echo", "\"Hello World\""}
	t.Logf("SSH'ing into the default device by running: %s %s", tools.fsshPath, fsshArgs)
	cmd = exec.Command(tools.fsshPath, fsshArgs...)

	sshOutput, err := cmd.Output()
	if err != nil {
		t.Fatalf("fssh returned unexpected error: %s", err)
	}

	sshOutputTrimmed := strings.TrimSpace(string(sshOutput))
	expectedOutput := "Hello World"
	if sshOutputTrimmed != expectedOutput {
		t.Errorf("fssh output doesn't match; Got: %s, want: %s", sshOutputTrimmed, expectedOutput)
	}

	// Reset the stdout and stderr buffers as they were previously used.
	resetBuffers(stdout, stderr)

	ffxTargetShowArgs := []string{"target", "show", "--json"}
	t.Logf("Trying to get target information by running: ffx %s", ffxTargetShowArgs)
	err = tools.ffxInstance.Run(ctx, ffxTargetShowArgs...)

	if err != nil {
		t.Fatalf("ffx target show returned unexpected error: %s", err)
	}

	ffxTargetShowErr := strings.TrimSpace(stderr.String())
	if ffxTargetShowErr != "" {
		t.Fatalf("ffx target show returned unexpected output to stderr: %s", ffxTargetShowErr)
	}

	isValid, err := verifyFFXTargetShowOutputWithDeviceName(stdout.Bytes(), deviceName)
	if err != nil {
		t.Errorf("verifyFFXTargetShowOutputWithDeviceName(%#v): got err %s", stdout.String(), err)
	}

	if !isValid {
		t.Errorf("verifyFFXTargetShowOutputWithDeviceName(%#v): output doesn't contain the expected device name %s", stdout.String(), deviceName)
	}
}
