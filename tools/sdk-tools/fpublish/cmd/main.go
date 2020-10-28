// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

// ExecCommand exports exec.Command as a variable so it can be mocked.
var ExecCommand = exec.Command

func main() {
	var sdk sdkcommon.SDKProperties
	sdk.Init()

	defaultRepoDir, err := sdk.GetDefaultPackageRepoDir()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not determine tools directory %v", err)
		os.Exit(1)
	}
	helpFlag := flag.Bool("help", false, "Show the usage message")
	verboseFlag := flag.Bool("verbose", false, "Print informational messages.")
	repoFlag := flag.String("repo-dir", defaultRepoDir, "Specify the path to the package repository.")
	flag.Parse()

	if *helpFlag {
		usage()
		os.Exit(0)
	}

	message, err := publish(sdk, *repoFlag, *verboseFlag)
	if err != nil {
		exiterr := err.(*exec.ExitError)
		log.Fatalf("%v%v", string(exiterr.Stderr), message)
		os.Exit(exiterr.ProcessState.ExitCode())
	}
	fmt.Println(message)
	os.Exit(0)
}

func publish(sdk sdkcommon.SDKProperties, repoPath string, verbose bool) (string, error) {
	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		log.Fatalf("Could not determine tools directory %v", err)
	}
	cmd := filepath.Join(toolsDir, "pm")
	args := []string{
		"publish", "-n", "-a", "-r", repoPath, "-f"}
	args = append(args, flag.Args()...)
	if verbose {
		args = append(args, "-v")
		fmt.Printf("Running command: %v %v\n", cmd, args)
	}
	output, err := ExecCommand(cmd, args...).CombinedOutput()
	return string(output), err
}

func usage() {
	fmt.Printf("Usage: %s <far-file>", filepath.Base(os.Args[0]))
	flag.PrintDefaults()
}
