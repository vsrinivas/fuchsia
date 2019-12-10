// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
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
	"go.fuchsia.dev/fuchsia/tools/lib/syslog"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/serial"

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
	Start(context.Context, []bootserver.Image, []string) error

	// Restart restarts the target.
	Restart(context.Context) error

	// Stop stops the target.
	Stop(context.Context) error

	// Wait waits for the target to finish running.
	Wait(context.Context) error
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

func (r *RunCommand) execute(ctx context.Context, args []string) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

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
		t, err := deriveTarget(ctx, obj, opts)
		if err != nil {
			return err
		}
		targets = append(targets, t)
	}
	if len(targets) == 0 {
		return fmt.Errorf("no targets found")
	}

	// This is the primary target that a command will be run against and that
	// logs will be streamed from.
	t0 := targets[0]

	errs := make(chan error)

	var socketPath string
	if t0.Serial() != nil {
		defer t0.Serial().Close()

		// Modify the zirconArgs passed to the kernel on boot to enable serial on x64.
		// arm64 devices should already be enabling kernel.serial at compile time.
		r.zirconArgs = append(r.zirconArgs, "kernel.serial=legacy")
		// Force serial output to the console instead of buffering it.
		r.zirconArgs = append(r.zirconArgs, "kernel.bypass-debuglog=true")

		sOpts := serial.ServerOptions{
			WriteBufferSize: botanist.SerialLogBufferSize,
		}
		if r.serialLogFile != "" {
			serialLog, err := os.Create(r.serialLogFile)
			if err != nil {
				return err
			}
			defer serialLog.Close()
			sOpts.AuxiliaryOutput = serialLog
		}

		s := serial.NewServer(t0.Serial(), sOpts)
		socketPath = createSocketPath()
		addr := &net.UnixAddr{Name: socketPath, Net: "unix"}
		l, err := net.ListenUnix("unix", addr)
		if err != nil {
			return err
		}
		defer l.Close()
		go func() {
			if err := s.Run(ctx, l); err != nil && ctx.Err() != nil {
				errs <- err
				return
			}
		}()
	}

	// Defer asynchronously restarts of each target.
	defer func() {
		var wg sync.WaitGroup
		for _, t := range targets {
			wg.Add(1)
			go func(t Target) {
				defer wg.Done()
				logger.Debugf(ctx, "stopping or rebooting the node %q\n", t.Nodename())
				if err := t.Stop(ctx); err == target.ErrUnimplemented {
					t.Restart(ctx)
				}
			}(t)
		}
		wg.Wait()
	}()

	// We wait until targets have started before running the subcommand against the zeroth one.
	var wg sync.WaitGroup
	for _, t := range targets {
		wg.Add(1)
		go func(t Target) {
			if err := t.Start(ctx, imgs, r.zirconArgs); err != nil {
				wg.Done()
				errs <- err
				return
			}
			wg.Done()
			if err := t.Wait(ctx); err != nil && err != target.ErrUnimplemented {
				errs <- err
			}
		}(t)
	}
	go func() {
		wg.Wait()
		errs <- r.runAgainstTarget(ctx, t0, args, socketPath)
	}()

	select {
	case err := <-errs:
		return err
	case <-ctx.Done():
	}
	return nil
}

func (r *RunCommand) runAgainstTarget(ctx context.Context, t Target, args []string, socketPath string) error {
	subprocessEnv := map[string]string{
		"FUCHSIA_NODENAME":      t.Nodename(),
		"FUCHSIA_SERIAL_SOCKET": socketPath,
	}

	// If |netboot| is true, then we assume that fuchsia is not provisioned
	// with a netstack; in this case, do not try to establish a connection.
	if !r.netboot {
		p, err := ioutil.ReadFile(t.SSHKey())
		if err != nil {
			return err
		}
		config, err := sshutil.DefaultSSHConfig(p)
		if err != nil {
			return err
		}
		client, err := sshutil.ConnectToNode(ctx, t.Nodename(), config)
		if err != nil {
			return err
		}
		defer client.Close()
		subprocessEnv["FUCHSIA_SSH_KEY"] = t.SSHKey()

		ip, err := t.IPv4Addr()
		if err != nil {
			logger.Errorf(ctx, "could not resolve IPv4 address of %s: %v", t.Nodename(), err)
		} else if ip != nil {
			logger.Infof(ctx, "IPv4 address of %s found: %s", t.Nodename(), ip)
			subprocessEnv["FUCHSIA_IPV4_ADDR"] = ip.String()
		}

		if r.syslogFile != "" {
			s, err := os.Create(r.syslogFile)
			if err != nil {
				return err
			}
			defer s.Close()
			syslogger := syslog.NewSyslogger(client, config)
			defer syslogger.Close()

			go func() {
				err := syslogger.Stream(ctx, s, false)
				// TODO(fxbug.dev/43518): when 1.13 is available, spell this as
				// `err != nil && errors.Is(err, context.Canceled) && errors.Is(err, context.DeadlineExceeded)`
				if err != nil && ctx.Err() == nil {
					logger.Errorf(ctx, "syslog streaming interrupted: %v", err)
				}
			}()
		}
	}

	// Run the provided command against t0, adding |subprocessEnv| into
	// its environment.
	environ := os.Environ()
	for k, v := range subprocessEnv {
		environ = append(environ, fmt.Sprintf("%s=%s", k, v))
	}
	runner := runner.SubprocessRunner{
		Env: environ,
	}

	ctx, cancel := context.WithTimeout(ctx, r.timeout)
	defer cancel()

	err := runner.Run(ctx, args, os.Stdout, os.Stderr)
	if ctx.Err() == context.DeadlineExceeded {
		return fmt.Errorf("command timed out after %v", r.timeout)
	}
	return err
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

func createSocketPath() string {
	// We randomly construct a socket path that is highly improbable to collide with anything.
	randBytes := make([]byte, 16)
	rand.Read(randBytes)
	return filepath.Join(os.TempDir(), "serial"+hex.EncodeToString(randBytes)+".sock")
}

func deriveTarget(ctx context.Context, obj []byte, opts target.Options) (Target, error) {
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
		return target.NewQEMUTarget(cfg, opts)
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
