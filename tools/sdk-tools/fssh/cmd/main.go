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

var (
	// ExecCommand exports exec.Command as a variable so it can be mocked.
	ExecCommand = exec.Command
)

func main() {
	var (
		err error
		sdk sdkcommon.SDKProperties
	)
	sdk.Init()

	helpFlag := flag.Bool("help", false, "Show the usage message")
	verboseFlag := flag.Bool("verbose", false, "Print informational messages.")

	// target related options
	privateKeyFlag := flag.String("private-key", "", "Uses additional private key when using ssh to access the device.")
	deviceNameFlag := flag.String("device-name", "", `Serves packages to a device with the given device hostname. Cannot be used with --device-ip."
		  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig.sh is used.`)
	deviceIPFlag := flag.String("device-ip", "", `Serves packages to a device with the given device ip address. Cannot be used with --device-name."
		  If neither --device-name nor --device-ip are specified, the device-name configured using fconfig.sh is used.`)
	sshConfigFlag := flag.String("sshconfig", "", "Use the specified sshconfig file instead of fssh's version.")

	flag.Parse()

	if *helpFlag {
		usage()
		os.Exit(0)
	}

	// Handle device name & device IP. If both are given, use name.
	deviceName := *deviceNameFlag
	deviceIP := ""
	if deviceName == "" {
		deviceIP = *deviceIPFlag
	}

	if deviceName == "" && deviceIP == "" {
		if deviceIP, err = sdk.GetDefaultDeviceIPAddress(); err != nil {
			log.Fatalf("Could not determine default device IP address: %v\n", err)
		}
		if deviceIP == "" {
			if deviceName, err = sdk.GetDefaultDeviceName(); err != nil {
				log.Fatalf("Could not determine default device name: %v\n", err)
			}
		}
	}
	if deviceIP == "" && deviceName == "" {
		log.Fatalf("--device-name or --device-ip needs to be set.\n")
	} else if deviceIP != "" {
		fmt.Fprintf(os.Stderr, "Using device address %v. Use --device-ip or fconfig to use another device.\n", deviceIP)
	} else {
		fmt.Fprintf(os.Stderr, "Using device name %v. Use --device-name or fconfig to use another device.\n", deviceName)
	}

	if err := ssh(sdk, *verboseFlag, deviceName, deviceIP, *sshConfigFlag, *privateKeyFlag, flag.Args()); err != nil {
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

func ssh(sdk sdkcommon.SDKProperties, verbose bool, deviceName string, deviceIP string, sshConfig string, privateKey string, args []string) error {
	var (
		err           error
		targetAddress string = deviceIP
	)
	if targetAddress == "" {
		targetAddress, err = sdk.GetAddressByName(deviceName)
		if err != nil {
			return fmt.Errorf("Cannot get target address for %v: %v", deviceName, err)
		}
	}
	if targetAddress == "" {
		return errors.New("Could not get target device IP address")
	}

	return sdk.RunSSHShell(targetAddress, sshConfig, privateKey, verbose, args)
}
