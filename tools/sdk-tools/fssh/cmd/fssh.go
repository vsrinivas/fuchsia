// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"

	"github.com/google/subcommands"
)

var (
	// ExecCommand exports exec.Command as a variable so it can be mocked.
	ExecCommand = exec.Command
)

const (
	privateKeyFlag = "private-key"
	deviceNameFlag = "device-name"
	deviceIPFlag   = "device-ip"
	sshConfigFlag  = "sshconfig"
	dataPathFlag   = "data-path"
	logLevelFlag   = "level"
	verboseFlag    = "verbose"

	logFlags = log.Ltime
)

type fsshCmd struct {
	// Target related options.
	privateKey string
	deviceName string
	deviceIP   string
	sshConfig  string
	dataPath   string

	logLevel logger.LogLevel
	verbose  bool
}

func (*fsshCmd) Name() string { return "fssh" }

func (*fsshCmd) Synopsis() string {
	return "Creates an SSH connection with a device and executes a command."
}

func (*fsshCmd) Usage() string {
	return fmt.Sprintf(`fssh [-%s device-name -%s device-ip -%s private-key -%s sshconfig -%s data-path -%s -%s log-level] [ssh_command]

Subcommands:
sync-keys        Sync SSH key files associated with Fuchsia between a local and remote workstation.
tunnel           Creates a tunnel between a local Fuchsia device and a remote host

`, deviceNameFlag, deviceIPFlag, privateKeyFlag, sshConfigFlag, dataPathFlag, verboseFlag, logLevelFlag)
}

func (c *fsshCmd) SetFlags(f *flag.FlagSet) {
	c.logLevel = logger.InfoLevel // Default that may be overridden.
	f.StringVar(&c.privateKey, privateKeyFlag, "", "Uses additional private key when using ssh to access the device.")
	f.StringVar(&c.deviceName, deviceNameFlag, "", `Serves packages to a device with the given device hostname. Cannot be used with --device-ip."
If neither --device-name nor --device-ip are specified, the device-name configured using ffx is used.`)
	f.StringVar(&c.deviceIP, deviceIPFlag, "", `Serves packages to a device with the given device ip address. Cannot be used with --device-name."
If neither --device-name nor --device-ip are specified, the device-name configured using ffx is used.`)
	f.StringVar(&c.sshConfig, sshConfigFlag, "", "Use the specified sshconfig file instead of fssh's version.")
	f.StringVar(&c.dataPath, dataPathFlag, "", "Specifies the data path for SDK tools. Defaults to $HOME/.fuchsia")
	f.Var(&c.logLevel, logLevelFlag, "Output verbosity, can be fatal, error, warning, info, debug or trace.")
	f.BoolVar(&c.verbose, verboseFlag, false, "Runs ssh in verbose mode.")
}

func (c *fsshCmd) Execute(_ context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	// Write all logs to stderr. Other tools parse the output of fssh which will break if logs
	// are written to stdout.
	log := logger.NewLogger(c.logLevel, color.NewColor(color.ColorAuto), os.Stderr, os.Stderr, "fssh ")
	log.SetFlags(logFlags)

	sdk, err := sdkcommon.NewWithDataPath(c.dataPath)
	if err != nil {
		log.Fatalf("Could not initialize SDK %v", err)
	}

	deviceConfig, err := sdk.ResolveTargetAddress(c.deviceIP, c.deviceName)
	if err != nil {
		log.Fatalf("%v", err)
	}
	log.Debugf("Using target address: %s", deviceConfig.DeviceIP)

	// If no deviceIPFlag was given, then get the SSH Port from the configuration.
	// We can't look at the configuration if the ip address was passed in since we don't have the
	// device name which is needed to look up the property.
	sshPort := ""
	if c.deviceIP == "" {
		sshPort = deviceConfig.SSHPort
		log.Debugf("Using sshport address: %s", sshPort)
		if sshPort == "22" {
			sshPort = ""
		}
	}

	log.Debugf("Running SSH with %s %s %s %s %t %s ", deviceConfig.DeviceIP, c.sshConfig,
		c.privateKey, sshPort, c.verbose, f.Args())
	if err := sdk.RunSSHShell(deviceConfig.DeviceIP, c.sshConfig, c.privateKey, sshPort, c.verbose, f.Args()); err != nil {
		var exitError *exec.ExitError
		// If there is an exit code, exit the subcommand with the same exit code.
		if errors.As(err, &exitError) {
			log.Errorf("Error running ssh: %v", err)
			os.Exit(exitError.ExitCode())
		} else {
			log.Fatalf("Error running ssh: %v", err)
		}
	}
	return subcommands.ExitSuccess
}
