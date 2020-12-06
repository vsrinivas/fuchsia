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

type sdkProvider interface {
	GetDefaultDeviceName() (string, error)
	GetFuchsiaProperty(deviceName string, property string) (string, error)
	GetAddressByName(deviceName string) (string, error)
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

	flag.Parse()

	if *helpFlag {
		usage()
		os.Exit(0)
	}

	if err := ssh(sdk, *verboseFlag, *deviceNameFlag, *deviceIPFlag, *sshConfigFlag, *privateKeyFlag, flag.Args()); err != nil {
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

func ssh(sdk sdkProvider, verbose bool, deviceName string, deviceIP string, sshConfig string, privateKey string, args []string) error {
	var (
		err           error
		targetAddress string
	)

	// If  there is a deviceIP address, use it.
	if deviceIP != "" {
		targetAddress = deviceIP
		fmt.Fprintf(os.Stderr, "Using device address %v. Use --device-ip or fconfig to use another device.\n", targetAddress)
	} else {
		// No explicit address, use the name
		if deviceName == "" {
			// No name passed in, use the default name.
			if deviceName, err = sdk.GetDefaultDeviceName(); err != nil {
				return fmt.Errorf("could not determine default device name: %v", err)
			}
		}
		if deviceName == "" {
			// No address specified, no device name specified, and no device configured as the default.
			return errors.New("invalid arguments. Need to specify --device-ip or --device-name or use fconfig to configure a default device")
		}

		fmt.Fprintf(os.Stderr, "Using device name %v. Use --device-name or fconfig to use another device.\n", deviceName)

		// look up a configured address by devicename
		targetAddress, err = sdk.GetFuchsiaProperty(deviceName, sdkcommon.DeviceIPKey)
		if err != nil {
			return fmt.Errorf("could not read configuration information for  %v: %v", deviceName, err)
		}
		// if still nothing, resolve the device address by name
		if targetAddress == "" {
			if targetAddress, err = sdk.GetAddressByName(deviceName); err != nil {
				return fmt.Errorf("cannot get target address for %v: %v", deviceName, err)
			}
		}
		if targetAddress == "" {
			return fmt.Errorf("could not get target device IP address for %v", deviceName)
		}
	}

	return sdk.RunSSHShell(targetAddress, sshConfig, privateKey, verbose, args)
}
