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
	ResolveTargetAddress(deviceIP string, deviceName string) (sdkcommon.DeviceConfig, error)
	RunFFX(args []string, interactive bool) (string, error)
}

var osExit = os.Exit

func main() {
	dataPathFlag := flag.String("data-path", "", "Specifies the data path for SDK tools. Defaults to $HOME/.fuchsia.")
	helpFlag := flag.Bool("help", false, "Show the usage message")
	verboseFlag := flag.Bool("verbose", false, "Print informational messages.")
	deviceNameFlag := flag.String("device-name", "", `Specifies the device name to use to look up configuration information regarding the package repo location. If not specified, the default device configured using fconfig is used.`)
	repoFlag := flag.String("repo-dir", "", "Specify the path to the package repository. If not specified, the default device configured using fconfig is used.")
	flag.Parse()

	sdk, err := sdkcommon.NewWithDataPath(*dataPathFlag)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not initialize SDK %v", err)
		osExit(1)
	}
	if *helpFlag {
		usage()
		osExit(0)
	}

	message, err := publish(sdk, *repoFlag, *deviceNameFlag, flag.Args(), *verboseFlag)
	if err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			log.Fatalf("%v: %v %v", string(exitError.Stderr), message, exitError)
			osExit(exitError.ProcessState.ExitCode())
		}
		log.Fatalf("%v%v", err, message)
		osExit(1)
	}
	fmt.Println(message)

	registerSymbolIndex(sdk, flag.Args(), *verboseFlag)

	osExit(0)
}

// Register the packages in the symbol index. Discard any failure.
func registerSymbolIndex(sdk sdkProvider, packages []string, verbose bool) {
	for _, pkg := range packages {
		// pkg should end with ".far", otherwise the publish function should fail.
		symbolIndexJsonFile := pkg[:len(pkg)-4] + ".symbol-index.json"
		if _, err := os.Stat(symbolIndexJsonFile); err != nil {
			// File doesn't exist or is not readable.
			continue
		}

		args := []string{"debug", "symbol-index", "add", symbolIndexJsonFile}
		if verbose {
			fmt.Printf("Running command: ffx %v\n", args)
		}
		// The command outputs nothing if succeeds, and outputs error messages if fails,
		// which is sufficient for our users. Use interactive=true here allows the
		// command to output.
		sdk.RunFFX(args, true)
	}
}

func publish(sdk sdkProvider, packageRepo string, deviceName string, packages []string, verbose bool) (string, error) {
	var err error
	repoPath := packageRepo

	deviceConfig, err := sdk.ResolveTargetAddress("", deviceName)
	if err != nil {
		return "", fmt.Errorf("Could not resolve device: %v", err)
	}

	if repoPath == "" {
		repoPath, err = sdk.GetFuchsiaProperty(deviceConfig.DeviceName, sdkcommon.PackageRepoKey)
		if err != nil {
			return "", fmt.Errorf("could not lookup package repo directory for %v: %v", deviceConfig, err)
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
	fmt.Printf("Usage: %s <far-file>\n", filepath.Base(os.Args[0]))
	flag.PrintDefaults()
}
