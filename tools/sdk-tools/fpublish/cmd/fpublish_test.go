// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

type testSDKProperties struct {
	dataPath   string
	err        error
	properties map[string]string
}

func (testSDK testSDKProperties) GetToolsDir() (string, error) {
	return "fake-tools", nil
}

func (testSDK testSDKProperties) GetFuchsiaProperty(deviceName string, property string) (string, error) {
	if testSDK.err != nil {
		return "", testSDK.err
	}
	var key string
	if deviceName != "" {
		key = fmt.Sprintf("%v.%v", deviceName, property)
	} else {
		key = property
	}
	return testSDK.properties[key], nil
}

// See exec_test.go for details, but effectively this runs the function called TestHelperProcess passing
// the args.
func helperCommandForFPublish(command string, s ...string) (cmd *exec.Cmd) {

	cs := []string{"-test.run=TestFakeFPublish", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

func TestFakeFPublish(*testing.T) {
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
	if filepath.Base(cmd) != "pm" {
		fmt.Fprintf(os.Stderr, "Unexpected command %v, expected 'pm'", cmd)
		os.Exit(1)
	}
	expected := strings.Split(os.Getenv("TEST_EXPECTED_ARGS"), ",")

	for i := range args {
		if args[i] != expected[i] {
			fmt.Fprintf(os.Stderr,
				"Mismatched args index %v. Expected: %v actual: %v\n",
				i, expected[i], args[i])
			fmt.Fprintf(os.Stderr, "Full args Expected: %v actual: %v",
				expected, args)
			os.Exit(3)
		}
	}

	os.Exit(0)

}

func TestFPublish(t *testing.T) {
	ExecCommand = helperCommandForFPublish
	defer func() { ExecCommand = exec.Command }()
	tests := []struct {
		repoDir      string
		deviceName   string
		packages     []string
		properties   map[string]string
		expectedArgs []string
	}{
		{
			repoDir:    "",
			deviceName: "",
			packages:   []string{"package.far"},
			properties: map[string]string{
				sdkcommon.PackageRepoKey: "/fake/repo/amber-files",
			},
			expectedArgs: []string{"publish", "-n", "-a", "-r", "/fake/repo/amber-files", "-f", "package.far"},
		},
		{
			repoDir:    "",
			deviceName: "test-device",
			packages:   []string{"package.far"},
			properties: map[string]string{
				sdkcommon.PackageRepoKey:                                "/fake/repo/amber-files",
				fmt.Sprintf("test-device.%v", sdkcommon.PackageRepoKey): "/fake/test-device/repo/amber-files",
			},
			expectedArgs: []string{"publish", "-n", "-a", "-r", "/fake/test-device/repo/amber-files", "-f", "package.far"},
		},
		{
			repoDir:      "/fake/repo/amber-files",
			deviceName:   "",
			packages:     []string{"package.far"},
			expectedArgs: []string{"publish", "-n", "-a", "-r", "/fake/repo/amber-files", "-f", "package.far"},
		},
		{
			repoDir:    "/fake/repo/amber-files",
			deviceName: "some-device",
			properties: map[string]string{
				sdkcommon.PackageRepoKey:                                "/invalid/repo/amber-files",
				fmt.Sprintf("some-device.%v", sdkcommon.PackageRepoKey): "/fake/some-device/repo/amber-files",
			},
			packages:     []string{"package.far"},
			expectedArgs: []string{"publish", "-n", "-a", "-r", "/fake/repo/amber-files", "-f", "package.far"},
		},
	}
	for i, test := range tests {
		os.Setenv("TEST_EXPECTED_ARGS", strings.Join(test.expectedArgs, ","))
		testSDK := testSDKProperties{dataPath: "/fake",
			properties: test.properties}
		t.Run(fmt.Sprintf("Test case %d", i), func(t *testing.T) {
			output, err := publish(testSDK, test.repoDir, test.deviceName, test.packages, false)
			if err != nil {
				t.Errorf("Error running fpublish: %v: %v",
					output, err)
			}
		})

	}
}
