// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver/lib"
	"go.fuchsia.dev/fuchsia/tools/botanist/lib"
	"go.fuchsia.dev/fuchsia/tools/botanist/target"
	"go.fuchsia.dev/fuchsia/tools/lib/command"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"

	"github.com/google/subcommands"
)

const (
	netstackTimeout time.Duration = 1 * time.Minute
)

// Target represents a fuchsia instance.
type Target interface {
	// Nodename returns the name of the target node.
	Nodename() string

	// IPv4Addr returns the IPv4 address of the target.
	IPv4Addr() (net.IP, error)

	// Serial returns the serial device associated with the target for serial i/o.
	Serial() io.ReadWriteCloser

	// SSHKey returns the private key corresponding an authorized SSH key of the target.
	SSHKey() string

	// Start starts the target.
	Start(ctx context.Context, images []bootserver.Image, args []string) error

	// Restart restarts the target.
	Restart(ctx context.Context) error

	// Stop stops the target.
	Stop(ctx context.Context) error

	// Wait waits for the target to finish running.
	Wait(ctx context.Context) error
}

// RunCommand is a Command implementation for booting a device and running a
// given command locally.
type RunCommand struct {
	// ConfigFile is the path to the target configurations.
	configFile string

	// ImageManifest is a path to an image manifest.
	imageManifest string

	// Netboot tells botanist to netboot (and not to pave).
	netboot bool

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

	// SshKey is the path to a private SSH user key.
	sshKey string

	// SerialLogFile, if nonempty, is the file where the system's serial logs will be written.
	serialLogFile string

	// RepoURL specifies the URL of a package repository.
	repoURL string

	// BlobURL optionally specifies the URL of where a package repository's blobs may be served from.
	// Defaults to $repoURL/blobs.
	blobURL string
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
	f.StringVar(&r.configFile, "config", "", "path to file of device config")
	f.StringVar(&r.imageManifest, "images", "", "path to an image manifest")
	f.BoolVar(&r.netboot, "netboot", false, "if set, botanist will not pave; but will netboot instead")
	f.Var(&r.zirconArgs, "zircon-args", "kernel command-line arguments")
	f.DurationVar(&r.timeout, "timeout", 10*time.Minute, "duration allowed for the command to finish execution.")
	f.StringVar(&r.cmdStdout, "stdout", "", "file to redirect the command's stdout into; if unspecified, it will be redirected to the process' stdout")
	f.StringVar(&r.cmdStderr, "stderr", "", "file to redirect the command's stderr into; if unspecified, it will be redirected to the process' stderr")
	f.StringVar(&r.syslogFile, "syslog", "", "file to write the systems logs to")
	f.StringVar(&r.sshKey, "ssh", "", "file containing a private SSH user key; if not provided, a private key will be generated.")
	f.StringVar(&r.serialLogFile, "serial-log", "", "file to write the serial logs to.")
	f.StringVar(&r.repoURL, "repo", "", "URL at which to configure a package repository")
	var defaultBlobURL string
	if r.repoURL != "" {
		defaultBlobURL = fmt.Sprintf("%s/blobs", r.repoURL)
	}
	f.StringVar(&r.blobURL, "blobs", defaultBlobURL, "URL at which to serve a package repository's blobs")
}

func (r *RunCommand) runCmd(ctx context.Context, args []string, t Target) error {
	nodename := t.Nodename()
	ip, err := t.IPv4Addr()
	if err == nil {
		logger.Infof(ctx, "IPv4 address of %s found: %s", nodename, ip)
	} else {
		logger.Errorf(ctx, "could not resolve IPv4 address of %s: %v", nodename, err)
	}

	env := append(
		os.Environ(),
		fmt.Sprintf("FUCHSIA_NODENAME=%s", nodename),
		fmt.Sprintf("FUCHSIA_IPV4_ADDR=%v", ip),
		fmt.Sprintf("FUCHSIA_SSH_KEY=%s", t.SSHKey()),
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

func getIndexedFilename(filename string, index int) string {
	ext := filepath.Ext(filename)
	name := filename[:len(filename)-len(ext)]
	return fmt.Sprintf("%s-%d%s", name, index, ext)
}

type targetSetup struct {
	targets    []Target
	syslogs    []*logWriter
	serialLogs []*logWriter
	err        error
}

type logWriter struct {
	name   string
	file   io.ReadWriteCloser
	target Target
}

func (r *RunCommand) setupTargets(ctx context.Context, imgs []bootserver.Image, targets []Target) *targetSetup {
	var syslogs, serialLogs []*logWriter
	errs := make(chan error, len(targets))
	var wg sync.WaitGroup
	var setupErr error

	for i, t := range targets {
		var syslog io.ReadWriteCloser
		var err error
		if r.syslogFile != "" {
			syslogFile := r.syslogFile
			if len(targets) > 1 {
				syslogFile = getIndexedFilename(r.syslogFile, i)
			}
			syslog, err = os.Create(syslogFile)
			if err != nil {
				setupErr = err
				break
			}
			syslogs = append(syslogs, &logWriter{
				name:   syslogFile,
				file:   syslog,
				target: t,
			})
		}

		zirconArgs := r.zirconArgs
		if t.Serial() != nil {
			if r.serialLogFile != "" {
				serialLogFile := r.serialLogFile
				if len(targets) > 1 {
					serialLogFile = getIndexedFilename(r.serialLogFile, i)
				}
				serialLog, err := os.Create(serialLogFile)
				if err != nil {
					setupErr = err
					break
				}
				serialLogs = append(serialLogs, &logWriter{
					name:   serialLogFile,
					file:   serialLog,
					target: t,
				})

				// Here we invoke the `dlog` command over serial to tail the existing log buffer into the
				// output file.  This should give us everything since Zedboot boot, and new messages should
				// be written to directly to the serial port without needing to tail with `dlog -f`.
				if _, err = io.WriteString(t.Serial(), "\ndlog\n"); err != nil {
					logger.Errorf(ctx, "failed to tail zedboot dlog: %v", err)
				}

				go func(t Target, serialLog io.ReadWriteCloser) {
					for {
						_, err := io.Copy(serialLog, t.Serial())
						if err != nil && err != io.EOF {
							logger.Errorf(ctx, "failed to write serial log: %v", err)
							return
						}
					}
				}(t, serialLog)
				zirconArgs = append(zirconArgs, "kernel.bypass-debuglog=true")
			}
			// Modify the zirconArgs passed to the kernel on boot to enable serial on x64.
			// arm64 devices should already be enabling kernel.serial at compile time.
			zirconArgs = append(zirconArgs, "kernel.serial=legacy")
		}

		wg.Add(1)
		go func(t Target, syslog io.Writer, zirconArgs []string) {
			defer wg.Done()
			if err := t.Start(ctx, imgs, zirconArgs); err != nil {
				errs <- err
				return
			}
			nodename := t.Nodename()

			if r.syslogFile != "" && syslog == nil {
				errs <- fmt.Errorf("syslog is nil.")
				return
			}

			// If having paved, SSH in and stream syslogs back to a file sink.
			if !r.netboot && syslog != nil {
				p, err := ioutil.ReadFile(t.SSHKey())
				if err != nil {
					errs <- err
					return
				}
				config, err := sshutil.DefaultSSHConfig(p)
				if err != nil {
					errs <- err
					return
				}
				client, err := sshutil.ConnectToNode(ctx, nodename, config)
				if err != nil {
					errs <- err
					return
				}
				syslogger, err := botanist.NewSyslogger(client)
				if err != nil {
					errs <- err
					return
				}
				go func() {
					syslogger.Stream(ctx, syslog)
					syslogger.Close()
				}()
			}
		}(t, syslog, zirconArgs)
	}
	// Wait for all targets to finish starting.
	wg.Wait()
	// We can close the channel on the receiver end since we wait for all target goroutines to finish.
	close(errs)
	err, ok := <-errs
	if ok {
		setupErr = err
	}
	return &targetSetup{
		targets:    targets,
		syslogs:    syslogs,
		serialLogs: serialLogs,
		err:        setupErr,
	}
}

func (r *RunCommand) runCmdWithTargets(ctx context.Context, targetSetup *targetSetup, args []string) error {
	for _, log := range targetSetup.syslogs {
		defer log.file.Close()
	}
	for _, log := range targetSetup.serialLogs {
		defer func(log *logWriter) {
			if err := log.target.Serial().Close(); err == nil {
				logger.Errorf(ctx, "serial output not closed yet.")
			}
			log.file.Close()
		}(log)
	}
	for _, t := range targetSetup.targets {
		defer func(t Target) {
			logger.Debugf(ctx, "stopping or rebooting the node %q\n", t.Nodename())
			if err := t.Stop(ctx); err == target.ErrUnimplemented {
				t.Restart(ctx)
			}
		}(t)
	}

	if targetSetup.err != nil {
		return targetSetup.err
	}

	errs := make(chan error)

	go func() {
		// Target doesn't matter for multi-device host tests. Just use first one.
		errs <- r.runCmd(ctx, args, targetSetup.targets[0])
	}()

	for _, t := range targetSetup.targets {
		go func(t Target) {
			if err := t.Wait(ctx); err != nil && err != target.ErrUnimplemented {
				errs <- err
			}
		}(t)
	}

	select {
	case err := <-errs:
		return err
	case <-ctx.Done():
	}
	return nil
}

func checkEmptyLogs(ctx context.Context, logs []*logWriter) error {
	for _, log := range logs {
		b, err := ioutil.ReadFile(log.name)
		if err != nil {
			return err
		}
		if len(b) == 0 {
			return fmt.Errorf("log is empty.")
		}
	}
	return nil
}

func (r *RunCommand) execute(ctx context.Context, args []string) error {
	var bootMode bootserver.Mode
	if r.netboot {
		bootMode = bootserver.ModeNetboot
	} else {
		bootMode = bootserver.ModePave
	}
	imgs, closeFunc, err := bootserver.GetImages(ctx, r.imageManifest, bootMode)
	if err != nil {
		return err
	}
	defer closeFunc()

	opts := target.Options{
		Netboot: r.netboot,
		SSHKey:  r.sshKey,
	}

	data, err := ioutil.ReadFile(r.configFile)
	if err != nil {
		return fmt.Errorf("could not open config file: %v", err)
	}
	var objs []json.RawMessage
	if err := json.Unmarshal(data, &objs); err != nil {
		return fmt.Errorf("could not unmarshal config file as a JSON list: %v", err)
	}

	var targets []Target
	for _, obj := range objs {
		t, err := DeriveTarget(ctx, obj, opts)
		if err != nil {
			return err
		}
		targets = append(targets, t)
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	targetSetup := r.setupTargets(ctx, imgs, targets)
	if err := r.runCmdWithTargets(ctx, targetSetup, args); err != nil {
		return err
	}
	return checkEmptyLogs(ctx, append(targetSetup.syslogs, targetSetup.serialLogs...))
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

func DeriveTarget(ctx context.Context, obj []byte, opts target.Options) (Target, error) {
	type typed struct {
		Type string `json:"type"`
	}
	var x typed

	if err := json.Unmarshal(obj, &x); err != nil {
		return nil, fmt.Errorf("object in list has no \"type\" field: %v", err)
	}
	switch x.Type {
	case "qemu":
		var cfg target.QEMUConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid QEMU config found: %v", err)
		}
		return target.NewQEMUTarget(cfg, opts), nil
	case "device":
		var cfg target.DeviceConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid device config found: %v", err)
		}
		t, err := target.NewDeviceTarget(ctx, cfg, opts)
		return t, err
	default:
		return nil, fmt.Errorf("unknown type found: %q", x.Type)
	}
}
