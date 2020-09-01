// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/botanist"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/botanist/target"
	"go.fuchsia.dev/fuchsia/tools/lib/environment"
	"go.fuchsia.dev/fuchsia/tools/lib/flagmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
	"go.fuchsia.dev/fuchsia/tools/lib/syslog"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/serial"

	"github.com/google/subcommands"
	"golang.org/x/sync/errgroup"
)

const (
	netstackTimeout    time.Duration = 1 * time.Minute
	serialSocketEnvKey               = "FUCHSIA_SERIAL_SOCKET"
)

// Target represents a fuchsia instance.
type Target interface {
	// Nodename returns the name of the target node.
	Nodename() string

	// IPv4Addr returns the IPv4 address of the target.
	IPv4Addr() (net.IP, error)

	// IPv6Addr returns the global unicast IPv6 address of the target.
	IPv6Addr() string

	// Serial returns the serial device associated with the target for serial i/o.
	Serial() io.ReadWriteCloser

	// SSHKey returns the private key corresponding an authorized SSH key of the target.
	SSHKey() string

	// Start starts the target.
	Start(context.Context, []bootserver.Image, []string) error

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
	zirconArgs flagmisc.StringsValue

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
	f.StringVar(&r.repoURL, "repo", "", "URL at which to configure a package repository; if the placeholder of \"localhost\" will be resolved and scoped as appropriate")
	f.StringVar(&r.blobURL, "blobs", "", "URL at which to serve a package repository's blobs; if the placeholder of \"localhost\" will be resolved and scoped as appropriate")
}

func (r *RunCommand) execute(ctx context.Context, args []string) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	opts := target.Options{
		Netboot: r.netboot,
		SSHKey:  r.sshKey,
	}

	data, err := ioutil.ReadFile(r.configFile)
	if err != nil {
		return fmt.Errorf("%s: %v", constants.ReadConfigFileErrorMsg, err)
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

	// Modify the zirconArgs passed to the kernel on boot to enable serial on x64.
	// arm64 devices should already be enabling kernel.serial at compile time.
	// We need to pass this in to all devices (even those without a serial line)
	// to prevent race conditions that only occur when the option isn't present.
	// TODO (fxb/10480): Move this back to being invoked in the if clause.
	r.zirconArgs = append(r.zirconArgs, "kernel.serial=legacy")

	// Disable usb mass storage to determine if it affects NUC stability.
	// TODO(rudymathu): Remove this once stability is achieved.
	r.zirconArgs = append(r.zirconArgs, "driver.usb_mass_storage.disable")

	// Disable xhci to determine if xhci rewrite is causing instability.
	// TODO(rudymathu): Remove this if/when xhci is stable.
	r.zirconArgs = append(r.zirconArgs, "driver.usb_xhci.disable")

	eg, ctx := errgroup.WithContext(ctx)
	socketPath := os.Getenv(serialSocketEnvKey)
	var conn net.Conn
	if socketPath != "" && r.serialLogFile != "" {
		// If a serial server was created earlier in the stack, use
		// the socket to copy to the serial log file.
		serialLog, err := os.Create(r.serialLogFile)
		if err != nil {
			return err
		}
		defer serialLog.Close()
		conn, err = net.Dial("unix", socketPath)
		if err != nil {
			return err
		}
		eg.Go(func() error {
			logger.Debugf(ctx, "starting serial collection")
			// Copy each line from the serial mux to the log file.
			b := bufio.NewReader(conn)
			for {
				line, err := b.ReadString('\n')
				if err != nil {
					if !serial.IsErrNetClosing(err) {
						return err
					}
					return nil
				}
				if _, err := io.WriteString(serialLog, line); err != nil {
					return err
				}
			}
		})
	} else if t0.Serial() != nil {
		// Otherwise, spin up a serial server now.
		defer t0.Serial().Close()

		sOpts := serial.ServerOptions{}
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
		eg.Go(func() error {
			if err := s.Run(ctx, l); err != nil && ctx.Err() == nil {
				return fmt.Errorf("serial server error: %w", err)
			}
			return nil
		})
	}

	for _, t := range targets {
		t := t
		eg.Go(func() error {
			if err := t.Wait(ctx); err != nil && err != target.ErrUnimplemented && ctx.Err() == nil {
				return err
			}
			return nil
		})
	}
	eg.Go(func() error {
		// Signal other goroutines to exit.
		defer cancel()
		if conn != nil {
			defer conn.Close()
		}

		if err := r.startTargets(ctx, targets); err != nil {
			return fmt.Errorf("%s: %w", constants.FailedToStartTargetMsg, err)
		}
		defer func() {
			ctx, cancel := context.WithTimeout(context.Background(), time.Minute)
			defer cancel()
			r.stopTargets(ctx, targets)
		}()
		return r.runAgainstTarget(ctx, t0, args, socketPath)
	})

	return eg.Wait()
}

func (r *RunCommand) startTargets(ctx context.Context, targets []Target) error {
	bootMode := bootserver.ModePave
	if r.netboot {
		bootMode = bootserver.ModeNetboot
	}

	// We wait until targets have started before running the subcommand against the zeroth one.
	eg, ctx := errgroup.WithContext(ctx)
	for _, t := range targets {
		t := t
		eg.Go(func() error {
			// TODO(fxb/47910): Move outside gofunc once we get rid of downloading or ensure that it only happens once.
			imgs, closeFunc, err := bootserver.GetImages(ctx, r.imageManifest, bootMode)
			if err != nil {
				return err
			}
			defer closeFunc()

			return t.Start(ctx, imgs, r.zirconArgs)
		})
	}
	return eg.Wait()
}

func (r *RunCommand) stopTargets(ctx context.Context, targets []Target) {
	// Stop the targets in parallel.
	eg, ctx := errgroup.WithContext(ctx)
	for _, t := range targets {
		t := t
		eg.Go(func() error {
			return t.Stop(ctx)
		})
	}
	_ = eg.Wait()
}

func (r *RunCommand) runAgainstTarget(ctx context.Context, t Target, args []string, socketPath string) error {
	subprocessEnv := map[string]string{
		"FUCHSIA_NODENAME":      t.Nodename(),
		"FUCHSIA_SERIAL_SOCKET": socketPath,
		"FUCHSIA_IPV6_ADDR":     t.IPv6Addr(),
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

		var client *sshutil.Client
		// TODO(fxb/52397): Determine whether this is necessary or there is a better
		// way to address this bug.
		if err = retry.Retry(ctx, retry.WithMaxAttempts(retry.NewConstantBackoff(5*time.Second), 2), func() error {
			// TODO(fxb/52397): Remove after done debugging.
			logger.Debugf(ctx, "creating SSH connection")
			client, err = sshutil.ConnectToNode(ctx, t.Nodename(), config)
			if err != nil {
				return err
			}

			subprocessEnv["FUCHSIA_SSH_KEY"] = t.SSHKey()

			ip, err := t.IPv4Addr()
			if err != nil {
				logger.Errorf(ctx, "could not resolve IPv4 address of %s: %v", t.Nodename(), err)
			} else if ip != nil {
				logger.Infof(ctx, "IPv4 address of %s found: %s", t.Nodename(), ip)
				subprocessEnv["FUCHSIA_IPV4_ADDR"] = ip.String()
			}

			if r.repoURL != "" {
				if err := botanist.AddPackageRepository(ctx, client, r.repoURL, r.blobURL); err != nil {
					logger.Errorf(ctx, "failed to set up a package repository: %v", err)
					client.Close()
					return err
				}
			}
			return nil
		}, nil); err != nil {
			return err
		}
		// This should generally only fail if the client has already closed by
		// the keep-alive goroutine, in which case this will return an error
		// that we can safely ignore.
		defer client.Close()

		if r.syslogFile != "" {
			s, err := os.Create(r.syslogFile)
			if err != nil {
				return err
			}
			defer s.Close()

			// Note: the syslogger takes ownership of the SSH client.
			syslogger := syslog.NewSyslogger(client)
			var wg sync.WaitGroup
			ctx, cancel := context.WithCancel(ctx)
			defer func() {
				// Signal syslogger.Stream to stop and wait for it to finish before return.
				// This makes sure syslogger.Stream finishes necessary clean-up (e.g. closing any open SSH sessions).
				// before SSH client is closed.
				cancel()
				wg.Wait()
				// Skip syslogger.Close() to avoid double close; syslogger.Close() only closes the underlying client,
				// which is closed in another defer above.
			}()
			wg.Add(1)
			go func() {
				defer wg.Done()
				if err := syslogger.Stream(ctx, s); err != nil && !errors.Is(err, ctx.Err()) {
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
		return fmt.Errorf("command %v timed out after %v", args, r.timeout)
	} else if err != nil {
		return fmt.Errorf("command %v failed: %w", args, err)
	}
	return nil
}

func (r *RunCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	args := f.Args()
	if len(args) == 0 {
		return subcommands.ExitUsageError
	}

	cleanUp, err := environment.Ensure()
	if err != nil {
		logger.Errorf(ctx, "failed to setup environment: %v", err)
		return subcommands.ExitFailure
	}
	defer cleanUp()

	var expandedArgs []string
	for _, arg := range args {
		expandedArgs = append(expandedArgs, os.ExpandEnv(arg))
	}
	r.blobURL = os.ExpandEnv(r.blobURL)
	r.repoURL = os.ExpandEnv(r.repoURL)
	if err := r.execute(ctx, expandedArgs); err != nil {
		logger.Errorf(ctx, "%v", err)
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
	case "aemu":
		var cfg target.QEMUConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid QEMU config found: %v", err)
		}
		return target.NewAEMUTarget(cfg, opts)
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
