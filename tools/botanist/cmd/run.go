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
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
	"go.fuchsia.dev/fuchsia/tools/lib/syslog"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/serial"

	"github.com/google/subcommands"
	"golang.org/x/sync/errgroup"
)

// RunCommand is a Command implementation for booting a device and running a
// given command locally.
type RunCommand struct {
	// ConfigFile is the path to the target configurations.
	configFile string

	// ImageManifest is a path to an image manifest.
	imageManifest string

	// flashScript is a path to a flash.sh file.
	flashScript string

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
	f.StringVar(&r.flashScript, "flash-script", "./flash.sh", "Path to flash.sh")
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
		return fmt.Errorf("%s: %w", constants.ReadConfigFileErrorMsg, err)
	}
	var objs []json.RawMessage
	if err := json.Unmarshal(data, &objs); err != nil {
		return fmt.Errorf("could not unmarshal config file as a JSON list: %w", err)
	}

	var targets []target.Target
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

	// Disable usb mass storage to determine if it affects NUC stability.
	// TODO(rudymathu): Remove this once stability is achieved.
	r.zirconArgs = append(r.zirconArgs, "driver.usb_mass_storage.disable")

	eg, ctx := errgroup.WithContext(ctx)
	socketPath := os.Getenv(constants.SerialSocketEnvKey)
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
					if !errors.Is(err, net.ErrClosed) {
						return fmt.Errorf("%s: %w", constants.SerialReadErrorMsg, err)
					}
					return nil
				}
				if _, err := io.WriteString(serialLog, line); err != nil {
					return fmt.Errorf("failed to write line to serial log: %w", err)
				}
			}
		})
	} else if t0.Serial() != nil {
		// Otherwise, spin up a serial server now.
		defer t0.Serial().Close()

		var sOpts serial.ServerOptions
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
				return fmt.Errorf("target %s failed: %w", t.Nodename(), err)
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

		if err := r.startTargets(ctx, targets, socketPath); err != nil {
			return fmt.Errorf("%s: %w", constants.FailedToStartTargetMsg, err)
		}
		for _, t := range targets {
			if err := r.addPackageRepo(ctx, t); err != nil {
				return err
			}
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

func (r *RunCommand) startTargets(ctx context.Context, targets []target.Target, serialSocketPath string) error {
	bootMode := bootserver.ModePave
	if r.netboot {
		bootMode = bootserver.ModeNetboot
	}

	// We wait until targets have started before running the subcommand against the zeroth one.
	eg, ctx := errgroup.WithContext(ctx)
	for _, t := range targets {
		t := t
		eg.Go(func() error {
			// TODO(fxbug.dev/47910): Move outside gofunc once we get rid of downloading or ensure that it only happens once.
			imgs, closeFunc, err := bootserver.GetImages(ctx, r.imageManifest, bootMode)
			if err != nil {
				return err
			}
			defer closeFunc()

			return t.Start(ctx, imgs, r.zirconArgs, serialSocketPath, r.flashScript)
		})
	}
	return eg.Wait()
}

func (r *RunCommand) stopTargets(ctx context.Context, targets []target.Target) {
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

// setupSSHEnvironment connects to the target over SSH, starts some background
// processes using the SSH connection (notably the package server and syslog
// streamer), and returns the necessary environment variables for a subprocess
// to be able to connect to the target via SSH.
//
// It returns a slice of "cleanup" functions that should be deferred in order
// (therefore called in reverse order) after the caller finishes running its
// command against the target device.
func (r *RunCommand) setupSSHEnvironment(ctx context.Context, t target.Target, socketPath string) (map[string]string, []func(), error) {
	var cleanups []func()
	env := make(map[string]string)
	p, err := ioutil.ReadFile(t.SSHKey())
	if err != nil {
		return nil, cleanups, err
	}
	config, err := sshutil.DefaultSSHConfig(p)
	if err != nil {
		return nil, cleanups, err
	}

	var addr net.IPAddr
	ipv4Addr, ipv6Addr, err := r.resolveTargetIP(ctx, t, socketPath)
	if err != nil {
		return nil, cleanups, err
	} else if ipv4Addr == nil && ipv6Addr.IP == nil {
		return nil, cleanups, fmt.Errorf("failed to resolve an IP address for %s", t.Nodename())
	}
	if ipv4Addr != nil {
		addr.IP = ipv4Addr
		logger.Debugf(ctx, "IPv4 address of %s found: %s", t.Nodename(), ipv4Addr)
		env[constants.IPv4AddrEnvKey] = ipv4Addr.String()
	}
	if ipv6Addr.IP != nil {
		// Prefer IPv6 if both IPv4 and IPv6 are available.
		addr = ipv6Addr
		logger.Debugf(ctx, "IPv6 address of %s found: %s", t.Nodename(), ipv6Addr)
		env[constants.IPv6AddrEnvKey] = ipv6Addr.String()
	}
	env[constants.DeviceAddrEnvKey] = addr.String()

	client, err := sshutil.NewClient(
		ctx,
		sshutil.ConstantAddrResolver{Addr: &net.TCPAddr{
			IP:   addr.IP,
			Zone: addr.Zone,
			Port: sshutil.SSHPort,
		}},
		config,
		sshutil.DefaultConnectBackoff(),
	)
	if err != nil {
		if err := r.dumpSyslogOverSerial(ctx, socketPath); err != nil {
			logger.Errorf(ctx, err.Error())
		}
		return nil, cleanups, err
	}
	cleanups = append(cleanups, client.Close)

	if r.syslogFile != "" {
		stopStreaming, err := r.startSyslogStream(ctx, client, t)
		if err != nil {
			if err := r.dumpSyslogOverSerial(ctx, socketPath); err != nil {
				logger.Errorf(ctx, err.Error())
			}
			return nil, cleanups, err
		}
		// Stop streaming syslogs after we've finished running the command.
		cleanups = append(cleanups, stopStreaming)
	}

	env[constants.SSHKeyEnvKey] = t.SSHKey()
	return env, cleanups, nil
}

func (r *RunCommand) addPackageRepo(ctx context.Context, t target.Target) error {
	if r.repoURL == "" {
		return nil
	}
	// TODO: This function really shouldn't need to resolve IP/create an SSH
	// client, but the existing code flow + the restrictive target interface
	// prevents an easy way to reuse this information across functions.
	// We should refactor this to avoid the overhead in the future.
	ipv4, ipv6, err := r.resolveTargetIP(ctx, t, "")
	if err != nil {
		return err
	}
	addr := net.IPAddr{IP: ipv4}
	if ipv6.IP != nil {
		addr = ipv6
	}
	p, err := ioutil.ReadFile(t.SSHKey())
	if err != nil {
		return err
	}
	config, err := sshutil.DefaultSSHConfig(p)
	if err != nil {
		return err
	}
	client, err := sshutil.NewClient(
		ctx,
		sshutil.ConstantAddrResolver{Addr: &net.TCPAddr{
			IP:   addr.IP,
			Zone: addr.Zone,
			Port: sshutil.SSHPort,
		}},
		config,
		sshutil.DefaultConnectBackoff(),
	)
	if err != nil {
		return err
	}
	defer client.Close()
	if err := botanist.AddPackageRepository(ctx, client, r.repoURL, r.blobURL); err != nil {
		return fmt.Errorf("%s: %w", constants.PackageRepoSetupErrorMsg, err)
	}
	return nil
}

func (r *RunCommand) getConfiguredTargetIP(t target.Target) (net.IP, bool) {
	if t, ok := t.(target.ConfiguredTarget); ok {
		if ip := t.Address(); len(ip) != 0 {
			return ip, true
		}
	}
	return nil, false
}

// dumpSyslogOverSerial runs log_listener over serial to collect logs that may
// help with debugging. This is intended to be used when SSH connection fails to
// get some information about the failure mode prior to exiting.
func (r *RunCommand) dumpSyslogOverSerial(ctx context.Context, socketPath string) error {
	socket, err := serial.NewSocket(ctx, socketPath)
	if err != nil {
		return fmt.Errorf("newSerialSocket failed: %w", err)
	}
	defer socket.Close()
	if err := serial.RunDiagnostics(ctx, socket); err != nil {
		return fmt.Errorf("failed to run serial diagnostics: %w", err)
	}
	// Dump the existing syslog buffer. This may not work if pkg-resolver is not
	// up yet, in which case it will just print nothing.
	cmds := []serial.Command{
		{Cmd: []string{syslog.LogListener, "--dump_logs", "yes"}, SleepDuration: 5 * time.Second},
	}
	if err := serial.RunCommands(ctx, socket, cmds); err != nil {
		return fmt.Errorf("failed to dump syslog over serial: %w", err)
	}
	return nil
}

func (r *RunCommand) runAgainstTarget(ctx context.Context, t target.Target, args []string, socketPath string) error {
	subprocessEnv := map[string]string{
		constants.NodenameEnvKey:     t.Nodename(),
		constants.SerialSocketEnvKey: socketPath,
	}

	// If |netboot| is true, then we assume that fuchsia is not provisioned
	// with a netstack; in this case, do not try to establish a connection.
	if !r.netboot {
		sshEnv, cleanups, err := r.setupSSHEnvironment(ctx, t, socketPath)
		for _, cleanup := range cleanups {
			defer cleanup()
		}
		if err != nil {
			return err
		}
		for k, v := range sshEnv {
			subprocessEnv[k] = v
		}
	}

	// Run the provided command against t0, adding |subprocessEnv| into
	// its environment.
	environ := os.Environ()
	for k, v := range subprocessEnv {
		environ = append(environ, fmt.Sprintf("%s=%s", k, v))
	}
	runner := subprocess.Runner{
		Env: environ,
	}

	ctx, cancel := context.WithTimeout(ctx, r.timeout)
	defer cancel()

	if err := runner.RunWithStdin(ctx, args, os.Stdout, os.Stderr, nil); err != nil {
		return fmt.Errorf("command %s with timeout %s failed: %w", args, r.timeout, err)
	}
	return nil
}

func (r *RunCommand) resolveTargetIP(ctx context.Context, t target.Target, socketPath string) (net.IP, net.IPAddr, error) {
	// If the target has a preconfigured IPv4, return it and avoid an expensive resolve.
	if configuredIP, ok := r.getConfiguredTargetIP(t); ok {
		return configuredIP, net.IPAddr{}, nil
	}
	ipv4Addr, ipv6Addr, err := func() (net.IP, net.IPAddr, error) {
		ctx, cancel := context.WithTimeout(ctx, 2*time.Minute)
		defer cancel()
		return botanist.ResolveIP(ctx, t.Nodename())
	}()
	if err != nil {
		// Invoke `threads` over serial if possible to dump process state to logs.
		if socket, err := serial.NewSocket(ctx, socketPath); err != nil {
			logger.Errorf(ctx, "newSerialSocket failed: %s", err)
		} else {
			defer socket.Close()
			if err := serial.RunDiagnostics(ctx, socket); err != nil {
				logger.Errorf(ctx, "failed to run diagnostics over serial socket: %s", err)
			}
		}
		return nil, net.IPAddr{}, fmt.Errorf("%s for %s: %w", constants.FailedToResolveIPErrorMsg, t.Nodename(), err)
	}
	return ipv4Addr, ipv6Addr, nil
}

// startSyslogStream uses the SSH client to start streaming syslogs from the
// fuchsia target to a file, in a background goroutine. It returns a function
// that cancels the streaming, which should be deferred by the caller.
func (r *RunCommand) startSyslogStream(ctx context.Context, client *sshutil.Client, t target.Target) (stopStreaming func(), err error) {
	syslogger := syslog.NewSyslogger(client)

	f, err := os.Create(r.syslogFile)
	if err != nil {
		return nil, err
	}

	ctx, cancel := context.WithCancel(ctx)

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer f.Close()
		defer wg.Done()
		errs := syslogger.Stream(ctx, f)
		// Check for errors in the stream to signify the device may have been rebooted.
		// In that case, we should re-add the package repo which may have been removed
		// by the reboot. The channel will be closed by the syslogger when it stops
		// streaming.
		for range errs {
			if !syslogger.IsRunning() {
				return
			}
			r.addPackageRepo(ctx, t)
		}
	}()

	// The caller should call this function when they want to stop streaming syslogs.
	return func() {
		// Signal syslogger.Stream to stop and wait for it to finish before
		// return. This makes sure syslogger.Stream finishes necessary cleanup
		// (e.g. closing any open SSH sessions) before SSH client is closed.
		cancel()
		wg.Wait()
	}, nil
}

func (r *RunCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	args := f.Args()
	if len(args) == 0 {
		return subcommands.ExitUsageError
	}

	cleanUp, err := environment.Ensure()
	if err != nil {
		logger.Errorf(ctx, "failed to setup environment: %s", err)
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
		logger.Errorf(ctx, "%s", err)
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

func deriveTarget(ctx context.Context, obj []byte, opts target.Options) (target.Target, error) {
	type typed struct {
		Type string `json:"type"`
	}
	var x typed

	if err := json.Unmarshal(obj, &x); err != nil {
		return nil, fmt.Errorf("object in list has no \"type\" field: %w", err)
	}
	switch x.Type {
	case "aemu":
		var cfg target.QEMUConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid QEMU config found: %w", err)
		}
		return target.NewAEMUTarget(cfg, opts)
	case "qemu":
		var cfg target.QEMUConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid QEMU config found: %w", err)
		}
		return target.NewQEMUTarget(cfg, opts)
	case "device":
		var cfg target.DeviceConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid device config found: %w", err)
		}
		t, err := target.NewDeviceTarget(ctx, cfg, opts)
		return t, err
	case "gce":
		var cfg target.GCEConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid GCE config found: %w", err)
		}
		return target.NewGCETarget(ctx, cfg, opts)
	default:
		return nil, fmt.Errorf("unknown type found: %q", x.Type)
	}
}
