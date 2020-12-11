// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"errors"
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

type sdkProvider interface {
	GetToolsDir() (string, error)
	GetFuchsiaProperty(deviceName string, property string) (string, error)
}

func main() {
	sdk, err := sdkcommon.New()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not initialize SDK %v", err)
		os.Exit(1)
	}

	helpFlag := flag.Bool("help", false, "Show the usage message")
	verboseFlag := flag.Bool("verbose", false, "Print informational messages.")
	deviceNameFlag := flag.String("device-name", "", `Specifies the device name to use to look up configuration information regarding the package repo location. If not specified, the default device configured using fconfig.sh is used.`)
	repoFlag := flag.String("repo-dir", "", "Specify the path to the package repository. If not specified, the default device configured using fconfig.sh is used.")
	flag.Parse()

	if *helpFlag {
		usage()
		os.Exit(0)
	}

	message, err := publish(sdk, *repoFlag, *deviceNameFlag, flag.Args(), *verboseFlag)
	if err != nil {
		exiterr := err.(*exec.ExitError)
		log.Fatalf("%v%v", string(exiterr.Stderr), message)
		os.Exit(exiterr.ProcessState.ExitCode())
	}
	fmt.Println(message)
	os.Exit(0)
}

func publish(sdk sdkProvider, packageRepo string, deviceName string, packages []string, verbose bool) (string, error) {
	var err error
	repoPath := packageRepo

	if repoPath == "" {
		repoPath, err = sdk.GetFuchsiaProperty(deviceName, sdkcommon.PackageRepoKey)
		if err != nil {
			return "", fmt.Errorf("could not lookup package repo directory %v", err)
		}
	}

	if repoPath == "" {
		return "", errors.New("could not determine package repo directory")
	}

	toolsDir, err := sdk.GetToolsDir()
	if err != nil {
		return "", fmt.Errorf("Could not determine tools directory %v", err)
	}
	cmd := filepath.Join(toolsDir, "pm")
	args := []string{
		"publish", "-n", "-a", "-r", repoPath, "-f"}
	args = append(args, packages...)
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
