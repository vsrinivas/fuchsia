// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

// See exec_test.go for details, but effectively this runs the function called TestHelperProcess passing
// the args.
func helperCommandForFPublish(command string, s ...string) (cmd *exec.Cmd) {
	//testenv.MustHaveExec(t)

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
	expected := []string{
		"publish", "-n", "-a", "-r", "/fake/repo/amber-files", "-f", "package.far",
	}
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
	testSDK := sdkcommon.SDKProperties{
		DataPath: "/fake",
	}
	ExecCommand = helperCommandForFPublish
	defer func() { ExecCommand = exec.Command }()
	output, err := publish(testSDK, "/fake/repo/amber-files", false)
	if err != nil {
		t.Fatalf("Error running fpublish: %v: %v",
			output, err)
	}
}
