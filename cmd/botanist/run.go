// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/botanist/target"
	"fuchsia.googlesource.com/tools/build"
	"fuchsia.googlesource.com/tools/command"
	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/netboot"
	"fuchsia.googlesource.com/tools/netutil"
	"fuchsia.googlesource.com/tools/runner"
	"fuchsia.googlesource.com/tools/sshutil"

	"github.com/google/subcommands"
	"golang.org/x/crypto/ssh"
)

const netstackTimeout time.Duration = 1 * time.Minute

// RunCommand is a Command implementation for booting a device and running a
// given command locally.
type RunCommand struct {
	// DeviceFile is the path to a file of device config.
	deviceFile string

	// ImageManifests is a list of paths to image manifests (e.g., images.json)
	imageManifests command.StringsFlag

	// Netboot tells botanist to netboot (and not to pave).
	netboot bool

	// Fastboot is a path to the fastboot tool. If set, botanist will flash
	// the device into zedboot.
	fastboot string

	// ZirconArgs are kernel command-line arguments to pass on boot.
	zirconArgs command.StringsFlag

	// Timeout is the duration allowed for the command to finish execution.
	timeout time.Duration

	// CmdStdout is the file to which the command's stdout will be redirected.
	cmdStdout string

	// CmdStderr is the file to which the command's stderr will be redirected.
	cmdStderr string

	// SysloggerFile, if nonempty, is the file to where the system's logs will be written.
	syslogFile string

	// sshKey is the path to a private SSH user key.
	sshKey string
}

func (*RunCommand) Name() string {
	return "run"
}

func (*RunCommand) Usage() string {
	return `
botanist run [flags...] [command...]

flags:
`
}

func (*RunCommand) Synopsis() string {
	return "boots a device and runs a local command"
}

func (r *RunCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&r.deviceFile, "device", "/etc/botanist/config.json", "path to file of device config")
	f.Var(&r.imageManifests, "images", "paths to image manifests")
	f.BoolVar(&r.netboot, "netboot", false, "if set, botanist will not pave; but will netboot instead")
	f.StringVar(&r.fastboot, "fastboot", "", "path to the fastboot tool; if set, the device will be flashed into Zedboot. A zircon-r must be supplied via -images")
	f.Var(&r.zirconArgs, "zircon-args", "kernel command-line arguments")
	f.DurationVar(&r.timeout, "timeout", 10*time.Minute, "duration allowed for the command to finish execution.")
	f.StringVar(&r.cmdStdout, "stdout", "", "file to redirect the command's stdout into; if unspecified, it will be redirected to the process' stdout")
	f.StringVar(&r.cmdStderr, "stderr", "", "file to redirect the command's stderr into; if unspecified, it will be redirected to the process' stderr")
	f.StringVar(&r.syslogFile, "syslog", "", "file to write the systems logs to")
	f.StringVar(&r.sshKey, "ssh", "", "file containing a private SSH user key; if not provided, a private key will be generated.")
}

func (r *RunCommand) runCmd(ctx context.Context, imgs build.Images, nodename string, args []string, privKeyFile string, signers []ssh.Signer, syslog io.Writer) error {
	// Set up log listener and dump kernel output to stdout.
	l, err := netboot.NewLogListener(nodename)
	if err != nil {
		return fmt.Errorf("cannot listen: %v", err)
	}
	go func() {
		defer l.Close()
		logger.Debugf(ctx, "starting log listener\n")
		for {
			data, err := l.Listen()
			if err != nil {
				continue
			}
			fmt.Print(data)
			select {
			case <-ctx.Done():
				return
			default:
			}
		}
	}()

	addr, err := netutil.GetNodeAddress(ctx, nodename, false)
	if err != nil {
		return err
	}

	// Boot fuchsia.
	var bootMode int
	if r.netboot {
		bootMode = botanist.ModeNetboot
	} else {
		bootMode = botanist.ModePave
	}
	if err = botanist.Boot(ctx, addr, bootMode, imgs, r.zirconArgs, signers); err != nil {
		return err
	}

	// If having paved, SSH in and stream syslogs back to a file sink.
	if !r.netboot && syslog != nil {
		privKey, err := ioutil.ReadFile(privKeyFile)
		if err != nil {
			return err
		}
		config, err := sshutil.DefaultSSHConfig(privKey)
		if err != nil {
			return err
		}
		client, err := sshutil.ConnectToNode(ctx, nodename, config)
		if err != nil {
			return err
		}
		syslogger, err := botanist.NewSyslogger(client)
		if err != nil {
			return err
		}
		go func() {
			syslogger.Stream(ctx, syslog)
			syslogger.Close()
		}()
	}

	ip, err := botanist.ResolveIPv4(ctx, nodename, netstackTimeout)
	if err == nil {
		logger.Infof(ctx, "IPv4 address of %s found: %s", nodename, ip)
	} else {
		logger.Errorf(ctx, "could not resolve IPv4 address of %s: %v", nodename, err)
	}

	env := append(
		os.Environ(),
		fmt.Sprintf("FUCHSIA_NODENAME=%s", nodename),
		fmt.Sprintf("FUCHSIA_IPV4_ADDR=%v", ip),
		fmt.Sprintf("FUCHSIA_SSH_KEY=%s", privKeyFile),
	)

	ctx, cancel := context.WithTimeout(ctx, r.timeout)
	defer cancel()

	stdout := os.Stdout
	if r.cmdStdout != "" {
		f, err := os.Create(r.cmdStdout)
		if err != nil {
			return err
		}
		defer f.Close()
		stdout = f
	}
	stderr := os.Stderr
	if r.cmdStderr != "" {
		f, err := os.Create(r.cmdStderr)
		if err != nil {
			return err
		}
		defer f.Close()
		stderr = f
	}

	runner := runner.SubprocessRunner{
		Env: env,
	}
	if err := runner.Run(ctx, args, stdout, stderr); err != nil {
		if ctx.Err() != nil {
			return fmt.Errorf("command timed out after %v", r.timeout)
		}
		return err
	}
	return nil
}

func (r *RunCommand) execute(ctx context.Context, args []string) error {
	imgs, err := build.LoadImages(r.imageManifests...)
	if err != nil {
		return fmt.Errorf("failed to load images: %v", err)
	}

	configs, err := target.LoadDeviceConfigs(r.deviceFile)
	if err != nil {
		return fmt.Errorf("failed to load device config file %q", r.deviceFile)
	} else if len(configs) != 1 {
		return fmt.Errorf("expected 1 entry in the device config file; found %d", len(configs))
	}
	config := configs[0]

	// If an SSH key is specified in the options, prepend it the configs list so that it
	// corresponds to the authorized key that would be paved.
	if r.sshKey != "" {
		config.SSHKeys = append([]string{r.sshKey}, config.SSHKeys...)
	}
	if len(config.SSHKeys) == 0 {
		return fmt.Errorf("SSH keys must be supplied in the config entry in %q or via -ssh", r.deviceFile)
	}
	var privKeys [][]byte
	for _, keyPath := range config.SSHKeys {
		p, err := ioutil.ReadFile(keyPath)
		if err != nil {
			return fmt.Errorf("could not read SSH key file %q: %v", keyPath, err)
		}
		privKeys = append(privKeys, p)
	}

	var signers []ssh.Signer
	for _, p := range privKeys {
		signer, err := ssh.ParsePrivateKey(p)
		if err != nil {
			return err
		}
		signers = append(signers, signer)
	}

	var syslog io.WriteCloser
	if r.syslogFile != "" {
		syslog, err = os.Create(r.syslogFile)
		if err != nil {
			return err
		}
		defer syslog.Close()
	}

	if config.Power != nil {
		defer func() {
			logger.Debugf(ctx, "rebooting the node %q\n", config.Nodename)

			if err := config.Power.RebootDevice(signers, config.Nodename); err != nil {
				logger.Errorf(ctx, "failed to reboot %q: %v\n", config.Nodename, err)
			}
		}()
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	errs := make(chan error)
	go func() {
		if r.fastboot != "" {
			zirconR := imgs.Get("zircon-r")
			if zirconR == nil {
				errs <- fmt.Errorf("zircon-r not provided")
				return
			}
			// If it can't find any fastboot device, the fastboot
			// tool will hang waiting, so we add a timeout.
			// All fastboot operations take less than a second on
			// a developer workstation, so two minutes to flash and
			// continue is very generous.
			ctx, cancel := context.WithTimeout(ctx, 2*time.Minute)
			defer cancel()
			logger.Debugf(ctx, "flashing to zedboot with fastboot\n")
			if err := botanist.FastbootToZedboot(ctx, r.fastboot, zirconR.Path); err != nil {
				errs <- err
				return
			}
		}
		errs <- r.runCmd(ctx, imgs, config.Nodename, args, config.SSHKeys[0], signers, syslog)
	}()

	select {
	case err := <-errs:
		return err
	case <-ctx.Done():
	}

	return nil
}

func (r *RunCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	args := f.Args()
	if len(args) == 0 {
		return subcommands.ExitUsageError
	}
	if err := r.execute(ctx, args); err != nil {
		logger.Errorf(ctx, "%v\n", err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
