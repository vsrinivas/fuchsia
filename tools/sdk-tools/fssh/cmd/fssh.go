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

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

var (
	// ExecCommand exports exec.Command as a variable so it can be mocked.
	ExecCommand = exec.Command
	// Logger level.
	level = logger.InfoLevel
)

const logFlags = log.Ltime

type sdkProvider interface {
	RunSSHShell(targetAddress string, sshConfig string, privateKey string, verbose bool, sshArgs []string) error
}

func main() {
	var err error

	sdk, err := sdkcommon.New()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not initialize SDK %v", err)
		os.Exit(1)
	}

	helpFlag := flag.Bool("help", false, "Show the usage message")
	verboseFlag := flag.Bool("verbose", false, "Print informational messages.")

	// target related options
	privateKeyFlag := flag.String("private-key", "", "Uses additional private key when using ssh to access the device.")
	deviceNameFlag := flag.String("device-name", "", `Serves packages to a device with the given device hostname. Cannot be used with --device-ip."
		  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig.sh is used.`)
	deviceIPFlag := flag.String("device-ip", "", `Serves packages to a device with the given device ip address. Cannot be used with --device-name."
		  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig.sh is used.`)
	sshConfigFlag := flag.String("sshconfig", "", "Use the specified sshconfig file instead of fssh's version.")
	flag.Var(&level, "level", "Output verbosity, can be fatal, error, warning, info, debug or trace.")

	flag.Parse()

	log := logger.NewLogger(level, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "fserve ")
	log.SetFlags(logFlags)

	if *helpFlag {
		usage()
		os.Exit(0)
	}

	targetAddress, err := sdk.ResolveTargetAddress(*deviceIPFlag, *deviceNameFlag)
	if err != nil {
		log.Fatalf("Could not determine target address with specified device-ip: %v, device-name: %v: %v", *deviceIPFlag, *deviceNameFlag, err)
	}
	log.Debugf("Using target address: %v", targetAddress)

	if err := ssh(sdk, *verboseFlag, targetAddress, *sshConfigFlag, *privateKeyFlag, flag.Args()); err != nil {
		var exitError *exec.ExitError
		if errors.As(err, &exitError) {
			os.Exit(exitError.ExitCode())
		} else {
			log.Fatalf("Error running ssh: %v", err)
		}
	}
	os.Exit(0)
}

func usage() {
	fmt.Printf("Usage: %s [options] [args]", filepath.Base(os.Args[0]))
	flag.PrintDefaults()
}

// ssh wraps sdk.RunSSHShell to enable testing by injecting an sdkProvider
func ssh(sdk sdkProvider, verbose bool, targetAddress string, sshConfig string, privateKey string, args []string) error {
	return sdk.RunSSHShell(targetAddress, sshConfig, privateKey, verbose, args)
}
