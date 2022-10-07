// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/botanist"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/botanist/targets"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/environment"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/flagmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/serial"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
	"go.fuchsia.dev/fuchsia/tools/lib/syslog"
	testrunnerconstants "go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"

	"github.com/google/subcommands"
	"golang.org/x/sync/errgroup"
)

// RunCommand is a Command implementation for booting a device and running a
// given command locally.
type RunCommand struct {
	// ConfigFile is the path to the target configurations.
	configFile string

	// DownloadManifest is the path we should write the package server's
	// download manifest to.
	downloadManifest string

	// ImageManifest is a path to an image manifest.
	imageManifest string

	// Netboot tells botanist to netboot (and not to pave).
	netboot bool

	// ZirconArgs are kernel command-line arguments to pass on boot.
	zirconArgs flagmisc.StringsValue

	// Timeout is the duration allowed for the command to finish execution.
	timeout time.Duration

	// syslogDir, if nonempty, is the directory in which system syslogs will be written.
	syslogDir string

	// SshKey is the path to a private SSH user key.
	sshKey string

	// serialLogDir, if nonempty, is the directory in which system serial logs will be written.
	serialLogDir string

	// RepoURL specifies the URL of a package repository.
	repoURL string

	// BlobURL optionally specifies the URL of where a package repository's blobs may be served from.
	// Defaults to $repoURL/blobs.
	blobURL string

	// localRepo specifies the path to a local package repository. If set,
	// botanist will spin up a package server to serve packages from this
	// repository.
	localRepo string

	// The path to the ffx tool.
	ffxPath string

	// The level of experimental ffx features to enable.
	ffxExperimentLevel int

	// A json struct following the build.ImageOverrides schema that defines the
	// images to override the defaults in images.json.
	imageOverrides imageOverridesFlagValue
}

type imageOverridesFlagValue build.ImageOverrides

func (v *imageOverridesFlagValue) String() string {
	data, err := json.Marshal(v)
	if err != nil {
		return ""
	}
	return string(data)
}

func (v *imageOverridesFlagValue) Set(s string) error {
	return json.Unmarshal([]byte(s), &v)
}

// targetInfo is the schema for a JSON object used to communicate target
// information (device properties, serial paths, SSH properties, etc.) to
// subprocesses.
type targetInfo struct {
	// Nodename is the Fuchsia nodename of the target.
	Nodename string `json:"nodename"`

	// IPv4 is the IPv4 address of the target.
	IPv4 string `json:"ipv4"`

	// IPv6 is the IPv6 address of the target.
	IPv6 string `json:"ipv6"`

	// SerialSocket is the path to the serial socket, if one exists.
	SerialSocket string `json:"serial_socket"`

	// SSHKey is a path to a private key that can be used to access the target.
	SSHKey string `json:"ssh_key"`
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
	f.StringVar(&r.syslogDir, "syslog-dir", "", "the directory to write all system logs to.")
	f.StringVar(&r.sshKey, "ssh", "", "file containing a private SSH user key; if not provided, a private key will be generated.")
	f.StringVar(&r.serialLogDir, "serial-log-dir", "", "the directory to write all serial logs to.")
	f.StringVar(&r.repoURL, "repo", "", "URL at which to configure a package repository; if the placeholder of \"localhost\" will be resolved and scoped as appropriate")
	f.StringVar(&r.blobURL, "blobs", "", "URL at which to serve a package repository's blobs; if the placeholder of \"localhost\" will be resolved and scoped as appropriate")
	f.StringVar(&r.localRepo, "local-repo", "", "path to a local package repository; the repo and blobs flags are ignored when this is set")
	f.StringVar(&r.ffxPath, "ffx", "", "Path to the ffx tool.")
	f.StringVar(&r.downloadManifest, "download-manifest", "", "Path to a manifest containing all package server downloads")
	f.IntVar(&r.ffxExperimentLevel, "ffx-experiment-level", 0, "The level of experimental features to enable. If -ffx is not set, this will have no effect.")
	f.Var(&r.imageOverrides, "image-overrides", "A json struct following the ImageOverrides schema at //tools/build/tests.go with the names of the images to use from images.json.")
}

func (r *RunCommand) execute(ctx context.Context, args []string) error {
	ctx, cancel := context.WithTimeout(ctx, r.timeout)
	go func() {
		<-ctx.Done()
		// Log the timeout for tefmocheck to detect it.
		if ctx.Err() == context.DeadlineExceeded {
			logger.Errorf(ctx, "%s (%s)", constants.CommandExceededTimeoutMsg, r.timeout)
		}
	}()
	defer cancel()

	// Parse targets out from the target configuration file.
	targetSlice, err := r.deriveTargetsFromFile(ctx)
	if err != nil {
		return err
	}
	// This is the primary target that a command will be run against and that
	// logs will be streamed from.
	t0 := targetSlice[0]
	ffxOutputsDir := filepath.Join(os.Getenv(testrunnerconstants.TestOutDirEnvKey), "ffx_outputs")
	removeFFXOutputsDir := false
	ffx, err := ffxutil.NewFFXInstance(ctx, r.ffxPath, "", []string{}, t0.Nodename(), t0.SSHKey(), ffxOutputsDir)
	if err != nil {
		return err
	}
	if ffx != nil {
		stdout, stderr, flush := botanist.NewStdioWriters(ctx)
		defer flush()
		ffx.SetStdoutStderr(stdout, stderr)
		defer func() {
			ffx.Stop()
			if removeFFXOutputsDir {
				os.RemoveAll(ffxOutputsDir)
			}
		}()
		if err := ffx.SetLogLevel(ctx, ffxutil.Trace); err != nil {
			return err
		}
		if err := ffx.Run(ctx, "config", "env"); err != nil {
			return err
		}
	}

	for _, t := range targetSlice {
		// Start serial servers for all targets. Will no-op for targets that
		// already have serial servers.
		if err := t.StartSerialServer(); err != nil {
			return err
		}
		// Attach an ffx instance for all targets. All ffx instances will use the same
		// config and daemon, but run commands against its own specified target.
		if ffx != nil {
			ffxForTarget, err := ffxutil.NewFFXInstance(ctx, r.ffxPath, "", []string{}, t.Nodename(), t.SSHKey(), ffxOutputsDir)
			if err != nil {
				return err
			}
			t.SetFFX(&targets.FFXInstance{ffxForTarget, r.ffxExperimentLevel}, ffx.Env())
		}
		if r.imageOverrides != nil {
			t.SetImageOverrides(build.ImageOverrides(r.imageOverrides))
		}
	}

	eg, ctx := errgroup.WithContext(ctx)
	if r.serialLogDir != "" {
		if err := os.Mkdir(r.serialLogDir, os.ModePerm); err != nil {
			return err
		}
		for _, t := range targetSlice {
			t := t
			eg.Go(func() error {
				logger.Debugf(ctx, "starting serial collection for target %s", t.Nodename())

				// Create a new file to capture the serial log for this nodename.
				serialLogName := fmt.Sprintf("%s_serial_log.txt", t.Nodename())
				// TODO(fxbug.dev/71529): Remove once there are no dependencies on this filename.
				if len(targetSlice) == 1 {
					serialLogName = "serial_log.txt"
				}
				serialLogPath := filepath.Join(r.serialLogDir, serialLogName)

				// Start capturing the serial log for this target.
				if err := t.CaptureSerialLog(serialLogPath); err != nil && ctx.Err() == nil {
					return err
				}
				return nil
			})
		}
	}

	// Run any preflights to prepare the testbed.
	if err := r.runPreflights(ctx); err != nil {
		return err
	}

	// Start up a local package server if one was requested.
	if r.localRepo != "" {
		var port int
		pkgSrvPort := os.Getenv(constants.PkgSrvPortKey)
		if pkgSrvPort == "" {
			logger.Warningf(ctx, "%s is empty, using default port %d", constants.PkgSrvPortKey, botanist.DefaultPkgSrvPort)
			port = botanist.DefaultPkgSrvPort
		} else {
			var err error
			port, err = strconv.Atoi(pkgSrvPort)
			if err != nil {
				return err
			}
		}
		pkgSrv, err := botanist.NewPackageServer(ctx, r.localRepo, r.repoURL, r.blobURL, r.downloadManifest, port)
		if err != nil {
			return err
		}
		defer pkgSrv.Close()
		// TODO(rudymathu): Once gcsproxy and remote package serving are deprecated, remove
		// the repoURL and blobURL from the command line flags.
		r.repoURL = pkgSrv.RepoURL
		r.blobURL = pkgSrv.BlobURL
	}
	// Disable usb mass storage to determine if it affects NUC stability.
	// TODO(rudymathu): Remove this once stability is achieved.
	r.zirconArgs = append(r.zirconArgs, "driver.usb_mass_storage.disable")

	// TODO(https://fxbug.dev/88370#c74): Remove this once CDC-ether flakiness
	// has been resolved.
	r.zirconArgs = append(r.zirconArgs, "driver.usb_cdc.log=debug")

	for _, t := range targetSlice {
		t := t
		eg.Go(func() error {
			if err := t.Wait(ctx); err != nil && err != targets.ErrUnimplemented && ctx.Err() == nil {
				return fmt.Errorf("target %s failed: %w", t.Nodename(), err)
			}
			return nil
		})
	}

	eg.Go(func() error {
		// Signal other goroutines to exit.
		defer cancel()
		if err := r.startTargets(ctx, targetSlice); err != nil {
			return fmt.Errorf("%s: %w", constants.FailedToStartTargetMsg, err)
		}
		logger.Debugf(ctx, "successfully started all targets")

		// Create a testbed config file. We have to do this after starting the
		// targets so that we can get their IP addresses.
		testbedConfig, err := r.createTestbedConfig(targetSlice)
		if err != nil {
			return err
		}
		defer os.Remove(testbedConfig)

		if !r.netboot {
			for _, t := range targetSlice {
				t := t
				client, err := t.SSHClient()
				if err != nil {
					if err := r.dumpSyslogOverSerial(ctx, t.SerialSocketPath()); err != nil {
						logger.Errorf(ctx, err.Error())
					}
					return err
				}
				if r.repoURL != "" {
					if err := t.AddPackageRepository(client, r.repoURL, r.blobURL); err != nil {
						return err
					}
					logger.Debugf(ctx, "added package repo to target %s", t.Nodename())
				}
				if r.syslogDir != "" {
					if _, err := os.Stat(r.syslogDir); errors.Is(err, os.ErrNotExist) {
						if err := os.Mkdir(r.syslogDir, os.ModePerm); err != nil {
							return err
						}
					}
					go func() {
						syslogName := fmt.Sprintf("%s_syslog.txt", t.Nodename())
						// TODO(fxbug.dev/71529): Remove when there are no dependencies on this filename.
						if len(targetSlice) == 1 {
							syslogName = "syslog.txt"
						}
						syslogPath := filepath.Join(r.syslogDir, syslogName)
						if err := t.CaptureSyslog(client, syslogPath, r.repoURL, r.blobURL); err != nil && ctx.Err() == nil {
							logger.Errorf(ctx, "failed to capture syslog at %s: %s", syslogPath, err)
						}
					}()
				}
			}
		}
		defer func() {
			ctx, cancel := context.WithTimeout(context.Background(), time.Minute)
			defer cancel()
			r.stopTargets(ctx, targetSlice)
		}()
		err = r.runAgainstTarget(ctx, t0, args, testbedConfig)
		// Cancel ctx to notify other goroutines that this routine has completed.
		// If another goroutine gets an error and the context is canceled, it
		// should return nil so that we always prioritize the result from this
		// goroutine.
		cancel()
		return err
	})

	err = eg.Wait()
	if err == nil {
		// In the case of a successful run, remove the ffx_outputs dir which contains ffx logs.
		// These logs can be very large, so we should only upload them in the case of a failure.
		removeFFXOutputsDir = true
	}
	return err
}

// runPreflights runs opaque preflight commands passed to botanist from
// the calling infrastructure.
func (r *RunCommand) runPreflights(ctx context.Context) error {
	logger.Debugf(ctx, "checking for preflights")
	botfilePath := os.Getenv("SWARMING_BOT_FILE")
	if botfilePath == "" {
		return nil
	}
	data, err := os.ReadFile(botfilePath)
	if err != nil {
		return err
	}
	if len(data) == 0 {
		// There were no commands in the botfile, exit out.
		return nil
	}
	type preflightCommands struct {
		Commands [][]string `json:"commands"`
	}
	var cmds preflightCommands
	if err := json.Unmarshal(data, &cmds); err != nil {
		return err
	}
	runner := subprocess.Runner{
		Env: os.Environ(),
	}
	for _, c := range cmds.Commands {
		logger.Debugf(ctx, "running preflight %s", c)
		if err := runner.RunWithStdin(ctx, c, os.Stdout, os.Stderr, nil); err != nil {
			return err
		}
	}
	// Some preflight commands can cause side effects that take up to 30s.
	time.Sleep(30 * time.Second)
	logger.Debugf(ctx, "done running preflights")
	return nil
}

// createTestbedConfig creates a configuration file that describes the targets
// attached and returns the path to the file.
func (r *RunCommand) createTestbedConfig(targetSlice []targets.Target) (string, error) {
	var testbedConfig []targetInfo
	for _, t := range targetSlice {
		cfg := targetInfo{
			Nodename:     t.Nodename(),
			SerialSocket: t.SerialSocketPath(),
		}
		if !r.netboot {
			cfg.SSHKey = t.SSHKey()
			if ipv4, err := t.IPv4(); err != nil {
				return "", err
			} else if ipv4 != nil {
				cfg.IPv4 = ipv4.String()
			}

			if ipv6, err := t.IPv6(); err != nil {
				return "", err
			} else if ipv6 != nil {
				cfg.IPv6 = ipv6.String()
			}
		}
		testbedConfig = append(testbedConfig, cfg)
	}

	data, err := json.Marshal(testbedConfig)
	if err != nil {
		return "", err
	}

	f, err := os.CreateTemp("", "testbed_config")
	if err != nil {
		return "", err
	}
	defer f.Close()
	if _, err := f.Write(data); err != nil {
		return "", err
	}
	return f.Name(), nil
}

func (r *RunCommand) startTargets(ctx context.Context, targetSlice []targets.Target) error {
	bootMode := bootserver.ModePave
	if r.netboot {
		bootMode = bootserver.ModeNetboot
	}

	// We wait until targets have started before running the subcommand against the zeroth one.
	eg, ctx := errgroup.WithContext(ctx)
	for _, t := range targetSlice {
		t := t
		eg.Go(func() error {
			// TODO(fxbug.dev/47910): Move outside gofunc once we get rid of downloading or ensure that it only happens once.
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

func (r *RunCommand) stopTargets(ctx context.Context, targetSlice []targets.Target) {
	// Stop the targets in parallel.
	var eg errgroup.Group
	for _, t := range targetSlice {
		t := t
		eg.Go(func() error {
			return t.Stop()
		})
	}
	_ = eg.Wait()
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

func (r *RunCommand) runAgainstTarget(ctx context.Context, t targets.Target, args []string, testbedConfig string) error {
	subprocessEnv := map[string]string{
		constants.NodenameEnvKey:      t.Nodename(),
		constants.SerialSocketEnvKey:  t.SerialSocketPath(),
		constants.ECCableEnvKey:       os.Getenv(constants.ECCableEnvKey),
		constants.TestbedConfigEnvKey: testbedConfig,
	}
	if t.UseFFX() {
		subprocessEnv[constants.FFXPathEnvKey] = r.ffxPath
		subprocessEnv[constants.FFXExperimentLevelEnvKey] = strconv.Itoa(r.ffxExperimentLevel)
	}

	// If |netboot| is true, then we assume that fuchsia is not provisioned
	// with a netstack; in this case, do not try to establish a connection.
	if !r.netboot {
		var addr net.IPAddr
		ipv6, err := t.IPv6()
		if err != nil {
			return err
		}
		if ipv6 != nil {
			addr = *ipv6
		}
		ipv4, err := t.IPv4()
		if err != nil {
			return err
		}
		if ipv4 != nil {
			addr.IP = ipv4
			addr.Zone = ""
		}
		env := map[string]string{
			constants.DeviceAddrEnvKey: addr.String(),
			constants.IPv4AddrEnvKey:   ipv4.String(),
			constants.IPv6AddrEnvKey:   ipv6.String(),
		}
		for k, v := range env {
			subprocessEnv[k] = v
		}
	}

	// One would assume this should only be provisioned when paving, but
	// there are some tests that attempt to SSH into a netbooted image that
	// has our SSH keys baked into it. Therefore, we add the SSH key to the
	// environment unconditionally. Additionally, some tools like FFX often
	// require the SSH key path to be absolute (https://fxbug.dev/101081).
	if t.SSHKey() != "" {
		absKeyPath, err := filepath.Abs(t.SSHKey())
		if err != nil {
			return err
		}
		subprocessEnv[constants.SSHKeyEnvKey] = absKeyPath
	}

	// Run the provided command against t0, adding |subprocessEnv| into
	// its environment.
	environ := os.Environ()
	for k, v := range subprocessEnv {
		environ = append(environ, fmt.Sprintf("%s=%s", k, v))
	}
	if t.UseFFX() {
		environ = append(environ, t.FFXEnv()...)
	}
	runner := subprocess.Runner{
		Env: environ,
	}

	stdout, stderr, flush := botanist.NewStdioWriters(ctx)
	defer flush()
	if err := runner.RunWithStdin(ctx, args, stdout, stderr, nil); err != nil {
		return fmt.Errorf("command %s with timeout %s failed: %w", args, r.timeout, err)
	}
	return nil
}

func (r *RunCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	args := f.Args()
	if len(args) == 0 {
		return subcommands.ExitUsageError
	}

	// If the TestOutDirEnvKey was set, that means botanist is being run in an infra
	// setting and thus needs an isolated environment.
	_, needsIsolatedEnv := os.LookupEnv(testrunnerconstants.TestOutDirEnvKey)
	cleanUp, err := environment.Ensure(needsIsolatedEnv)
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

func (r *RunCommand) deriveTargetsFromFile(ctx context.Context) ([]targets.Target, error) {
	opts := targets.Options{
		Netboot: r.netboot,
		SSHKey:  r.sshKey,
	}

	data, err := os.ReadFile(r.configFile)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", constants.ReadConfigFileErrorMsg, err)
	}
	var objs []json.RawMessage
	if err := json.Unmarshal(data, &objs); err != nil {
		return nil, fmt.Errorf("could not unmarshal config file as a JSON list: %w", err)
	}

	var targetSlice []targets.Target
	for _, obj := range objs {
		t, err := targets.DeriveTarget(ctx, obj, opts)
		if err != nil {
			return nil, err
		}
		targetSlice = append(targetSlice, t)
	}
	if len(targetSlice) == 0 {
		return nil, fmt.Errorf("no targets found")
	}
	return targetSlice, nil
}
