// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"os/signal"
	"syscall"
	"time"

	"fuchsia.googlesource.com/tools/botanist"
	"fuchsia.googlesource.com/tools/build"
	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/netboot"
	"fuchsia.googlesource.com/tools/pdu"

	"github.com/google/subcommands"
	"golang.org/x/crypto/ssh"
)

// RunCommand is a Command implementation for booting a device and running a
// given command locally.
type RunCommand struct {
	// DeviceFile is the path to a file of device properties.
	deviceFile string

	// ImageManifests is a list of paths to image manifests (e.g., images.json)
	imageManifests botanist.StringsFlag

	// Netboot tells botanist to netboot (and not to pave).
	netboot bool

	// Fastboot is a path to the fastboot tool. If set, botanist will flash
	// the device into zedboot.
	fastboot string

	// ZirconArgs are kernel command-line arguments to pass on boot.
	zirconArgs botanist.StringsFlag

	// Timeout is the duration allowed for the command to finish execution.
	timeout time.Duration

	// CmdStdout is the file to which the command's stdout will be redirected.
	cmdStdout string

	// CmdStderr is the file to which the command's stderr will be redirected.
	cmdStderr string

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
	f.StringVar(&r.deviceFile, "device", "/etc/botanist/config.json", "path to file of device properties")
	f.Var(&r.imageManifests, "images", "paths to image manifests")
	f.BoolVar(&r.netboot, "netboot", false, "if set, botanist will not pave; but will netboot instead")
	f.StringVar(&r.fastboot, "fastboot", "", "path to the fastboot tool; if set, the device will be flashed into Zedboot. A zircon-r must be supplied via -images")
	f.Var(&r.zirconArgs, "zircon-args", "kernel command-line arguments")
	f.DurationVar(&r.timeout, "timeout", 10*time.Minute, "duration allowed for the command to finish execution.")
	f.StringVar(&r.cmdStdout, "stdout", "", "file to redirect the command's stdout into; if unspecified, it will be redirected to the process' stdout")
	f.StringVar(&r.cmdStderr, "stderr", "", "file to redirect the command's stderr into; if unspecified, it will be redirected to the process' stderr")
	f.StringVar(&r.sshKey, "ssh", "", "file containing a private SSH user key; if not provided, a private key will be generated.")
}

func (r *RunCommand) runCmd(ctx context.Context, imgs build.Images, nodename string, args []string, privKey []byte) error {
	// Set up log listener and dump kernel output to stdout.
	l, err := netboot.NewLogListener(nodename)
	if err != nil {
		return fmt.Errorf("cannot listen: %v\n", err)
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

	addr, err := botanist.GetNodeAddress(ctx, nodename, false)
	if err != nil {
		return err
	}

	signer, err := ssh.ParsePrivateKey(privKey)
	if err != nil {
		return err
	}
	authorizedKey := ssh.MarshalAuthorizedKey(signer.PublicKey())

	// Boot fuchsia.
	var bootMode int
	if r.netboot {
		bootMode = botanist.ModeNetboot
	} else {
		bootMode = botanist.ModePave
	}
	if err = botanist.Boot(ctx, addr, bootMode, imgs, r.zirconArgs, authorizedKey); err != nil {
		return err
	}

	env := os.Environ()
	env = append(
		env,
		fmt.Sprintf("NODENAME=%s", nodename),
		fmt.Sprintf("SSH_KEY=%s", string(privKey)),
	)

	// Run command.
	// The subcommand is put in its own process group so that no subprocesses it spins up
	// are orphaned on cancelation.
	ctx, cancel := context.WithTimeout(ctx, r.timeout)
	defer cancel()
	cmd := exec.Cmd{
		Path:        args[0],
		Args:        args,
		Env:         env,
		SysProcAttr: &syscall.SysProcAttr{Setpgid: true},
		Stdout: os.Stdout,
		Stderr: os.Stderr,
	}

	if r.cmdStdout != "" {
		f, err := os.Create(r.cmdStdout)
		if err != nil {
			return err
		}
		defer f.Close()
		cmd.Stdout = f
	}
	if r.cmdStderr != "" {
		f, err := os.Create(r.cmdStderr)
		if err != nil {
			return err
		}
		defer f.Close()
		cmd.Stderr = f
	}

	if err := cmd.Start(); err != nil {
		return err
	}
	done := make(chan error)
	go func() {
		done <- cmd.Wait()
	}()

	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
	}
	return fmt.Errorf("command timed out after %v", r.timeout)
}

func (r *RunCommand) execute(ctx context.Context, args []string) error {
	imgs, err := build.LoadImages(r.imageManifests...)
	if err != nil {
		return fmt.Errorf("failed to load images: %v", err)
	}

	var privKey []byte
	if r.sshKey == "" {
		privKey, err = botanist.GeneratePrivateKey()
		if err != nil {
			return err
		}
	} else {
		privKey, err = ioutil.ReadFile(r.sshKey)
		if err != nil {
			return err
		}
	}

	var properties botanist.DeviceProperties
	if err := botanist.LoadDeviceProperties(r.deviceFile, &properties); err != nil {
		return fmt.Errorf("failed to open device properties file \"%v\": %v", r.deviceFile, err)
	}

	if properties.PDU != nil {
		defer func() {
			logger.Debugf(ctx, "rebooting the node %q\n", properties.Nodename)

			if err := pdu.RebootDevice(properties.PDU); err != nil {
				logger.Errorf(ctx, "failed to reboot %q: %v\n", properties.Nodename, err)
			}
		}()
	}

	ctx, cancel := context.WithCancel(ctx)

	// Handle SIGTERM and make sure we send a reboot to the device.
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGTERM)

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
		errs <- r.runCmd(ctx, imgs, properties.Nodename, args, privKey)
	}()

	select {
	case err := <-errs:
		return err
	case <-signals:
		cancel()
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
