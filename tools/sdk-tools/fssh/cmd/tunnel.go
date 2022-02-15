// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"syscall"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/fssh/synckeys"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/fssh/tunnel"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"

	"github.com/google/subcommands"
)

const (
	printSSHConfigFlag = "print-ssh-config"
	tunnelPortsFlag    = "tunnel-ports"
)

type sdkProvider interface {
	GetSDKDataPath() string
	ResolveTargetAddress(deviceIP string, deviceName string) (sdkcommon.DeviceConfig, error)
}

type tunnelCmd struct {
	remoteHost     string
	deviceIP       string
	dataPath       string
	deviceName     string
	sshConfig      string
	printSSHConfig bool
	verbose        bool
	tunnelPorts    intSlice
	logLevel       logger.LogLevel
}

func (*tunnelCmd) Name() string { return "tunnel" }

func (*tunnelCmd) Synopsis() string {
	return "Create a tunnel between a local Fuchsia device and a remote host."
}

func (*tunnelCmd) Usage() string {
	return fmt.Sprintf(`fssh tunnel [-%s remote-host -%s device-ip -%s -device-name -%s path-to-ssh-config -%s=NNNN,NNNN -s]:
Creates tunnel between the specified remote host and local Fuchsia device. Either the %s or %s flag must be set. If both are set the %s flag will take precedence.
`, remoteHostFlag, deviceIPFlag, deviceNameFlag, sshConfigFlag, tunnelPortsFlag, deviceIPFlag, deviceNameFlag, deviceIPFlag)
}

func (c *tunnelCmd) SetFlags(f *flag.FlagSet) {
	c.logLevel = logger.InfoLevel // Default that may be overridden.
	f.Var(&c.logLevel, logLevelFlag, "Output verbosity, can be fatal, error, warning, info, debug or trace.")
	f.StringVar(&c.remoteHost, remoteHostFlag, "", "The remote host where development is taking place. If this flag is missing the most recent value from a previous call to this command will be used if applicable.")
	f.StringVar(&c.deviceIP, deviceIPFlag, "", fmt.Sprintf("The IPv6 address of the target device to use to create the tunnel. This flag will override the %s flag if provided.", deviceNameFlag))
	f.StringVar(&c.deviceName, deviceNameFlag, "", fmt.Sprintf("The name of the target device to use to create the tunnel. If provided the value provided for the flag %s will take presedence over this flag.", deviceIPFlag))
	f.StringVar(&c.dataPath, dataPathFlag, "", "Specifies the data path for SDK tools. Defaults to $HOME/.fuchsia")
	f.StringVar(&c.sshConfig, sshConfigFlag, "", fmt.Sprintf("Optional. Path to a SSH configuration file to use in leiu of the default SSH config. Run 'fssh tunnel -%s' to view the default SSH configuration.", printSSHConfigFlag))
	f.Var(&c.tunnelPorts, tunnelPortsFlag, fmt.Sprintf(`Optional. Comma separated list of additional ports to forward when setting up the tunnel. It is an error to specify a protected port (any port less than 1024).
If using the default SSH config, the following ports which are already in use will be ignored: %s.`, usedPortsString()))
	f.BoolVar(&c.printSSHConfig, printSSHConfigFlag, false, "Print the SSH config instead of setting up the tunnel.")
	f.BoolVar(&c.verbose, verboseFlag, false, "Add debugging to the SSH config.")
}

func (c *tunnelCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if c.verbose {
		c.logLevel = logger.DebugLevel
	}
	log := logger.NewLogger(c.logLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "tunnel ")
	log.SetFlags(logFlags)
	ctx = logger.WithLogger(ctx, log)

	logger.Debugf(ctx, "Running the tunnel command...")
	sdk, err := sdkcommon.NewWithDataPath(c.dataPath)
	if err != nil {
		log.Fatalf("Could not initialize SDK %v", err)
	}

	sshConfigContents, err := c.parseFlags(ctx, sdk)
	if err != nil {
		log.Fatalf("Could not parse flags: %v", err)
	}

	log.Infof("Using remote host %q", c.remoteHost)
	log.Infof("Using Fuchsia device IP %q and name %q", c.deviceIP, c.deviceName)

	if c.sshConfig != "" {
		logger.Debugf(ctx, "Using SSH config file %q", c.sshConfig)
	} else {
		logger.Debugf(ctx, "No SSH config provided. Using default SSH config.")
	}

	if c.printSSHConfig {
		log.Infof("Printing SSH config:")
		log.Infof(string(sshConfigContents))
		return subcommands.ExitSuccess
	}

	// Clean up any existing tunnel or sockets that maybe around from a previous tunnel.
	logger.Debugf(ctx, "Checking remote for existing tunnel")
	result, err := tunnel.CleanupTunnel(ctx, "", c.remoteHost)
	if err != nil {
		log.Warningf("Error cleaning up existing tunnel: %v", err)
	}
	if result != "" {
		log.Infof(result)
	}

	logger.Debugf(ctx, "Syncing Fuchsia SSH keys between this machine and %q...", c.remoteHost)
	if err := synckeys.Fuchsia(ctx, c.remoteHost); err != nil {
		log.Fatalf("Fuchsia SSH keys between this machine and %q are not in sync: %v", c.remoteHost, err)
	}

	log.Infof("Starting SSH tunnel...")
	cmd, err := tunnel.Cmd("", c.sshConfig, c.remoteHost)
	if err != nil {
		log.Fatalf("Could not create SSH tunnel command: %s", err)
	}
	cmd.Stdout = os.Stdout

	if err := pipeStderrToFatalLog(ctx, cmd, c.verbose); err != nil {
		log.Fatalf("Could not pipe command standard err to logger: %s", err)
	}
	if err := cmd.Start(); err != nil {
		log.Fatalf("Could not start the SSH tunnel: %s", err)
	}

	// If we're using the default config, pass in a flag
	// to indicate it needs to be cleaned up when exiting.
	cleanupSSHConfig := len(sshConfigContents) != 0
	c.listenForInterrupt(ctx, cmd, cleanupSSHConfig)
	log.Infof("SSH tunnel starting. Press Ctrl+C to exit.")
	if err = cmd.Wait(); err != nil {
		log.Infof("Tunnel closed with exit code %d", cmd.ProcessState.ExitCode())
	}

	return subcommands.ExitSuccess
}

func isThisFailedConnectionPortMessage(message string) bool {
	r := regexp.MustCompile(`connect_to .* port [\d]*: failed.`)
	return r.MatchString(message)
}

func pipeStderrToFatalLog(ctx context.Context, cmd *exec.Cmd, verbose bool) error {
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}
	go func() {
		in := bufio.NewScanner(stderr)
		for in.Scan() {
			errorMessage := in.Text()
			if !verbose && isThisFailedConnectionPortMessage(errorMessage) {
				logger.Debugf(ctx, "%s", errorMessage)
			} else {
				logger.Errorf(ctx, "%s", errorMessage)
			}
		}
		if err := in.Err(); err != nil {
			// Usually the error here is that the pipe is closed
			// which happens during shutdown, so just log the error.
			logger.Debugf(ctx, "%v", err)
		}
	}()
	return nil
}

// listenForInterrupt starts a separate goroutine to listen for the user to
// press Ctrl+C.
func (c *tunnelCmd) listenForInterrupt(ctx context.Context, cmd *exec.Cmd, cleanupSSHConfig bool) {
	channel := make(chan os.Signal)
	signal.Notify(channel, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-channel
		// Remove temporary SSH config file after command completes.
		if cleanupSSHConfig {
			if err := os.Remove(c.sshConfig); err != nil {
				logger.Debugf(ctx, "Error removing SSH config file %q: %s", c.sshConfig, err)
			}
		}
		logger.Infof(ctx, "SSH tunnel shut down.")
	}()
}

// parseFlags parses flags from the user and sets default values.
func (c *tunnelCmd) parseFlags(ctx context.Context, sdk sdkProvider) ([]byte, error) {
	deviceConfig, err := sdk.ResolveTargetAddress(c.deviceIP, c.deviceName)
	if err != nil {
		return []byte{}, err
	}
	c.deviceIP = deviceConfig.DeviceIP
	c.deviceName = deviceConfig.DeviceName

	usingCachedName := false
	if c.remoteHost == "" {
		c.remoteHost = getCachedRemoteHost(ctx, sdk.GetSDKDataPath())
		usingCachedName = c.remoteHost != ""
	}

	// validate hostname
	if c.remoteHost == "" {
		return []byte{}, fmt.Errorf("No remote host provided. Please add the '-%s' flag", remoteHostFlag)
	}

	if !validHostname(c.remoteHost) {
		// if the host name came from the cache, remove it.
		if usingCachedName {
			setCachedRemoteHost(ctx, sdk.GetSDKDataPath(), "")
		}
		return []byte{}, fmt.Errorf("%s is not a valid host name", c.remoteHost)
	}

	// cache the name if needed
	if !usingCachedName {
		setCachedRemoteHost(ctx, sdk.GetSDKDataPath(), c.remoteHost)
	}
	var sshConfigContents []byte
	if c.sshConfig == "" {
		sshConfigContents, err = tunnel.GenerateSSHConfig(tunnel.DefaultSSHConfigTemplate, c.remoteHost, c.deviceIP, []int(c.tunnelPorts), c.verbose)
		if err != nil {
			return []byte{}, fmt.Errorf("Could not generate default SSH config: %s", err)
		}
		path, err := sdkcommon.WriteTempFile(sshConfigContents)
		if err != nil {
			return []byte{}, fmt.Errorf("Could not write default SSH config to a temporary file: %s", err)
		}
		c.sshConfig = path
	} else {
		info, err := os.Stat(c.sshConfig)
		if os.IsNotExist(err) {
			return []byte{}, fmt.Errorf("unable to locate specified SSH config file %q: %s", c.sshConfig, err)
		}
		if info.IsDir() {
			return []byte{}, fmt.Errorf("path to SSH config file %q is a direcectory", c.sshConfig)
		}
	}
	return sshConfigContents, nil
}

func getCachedRemoteHost(ctx context.Context, dataPath string) string {
	if dataPath == "" {
		return ""
	}
	path := filepath.Join(dataPath, remoteHostFlag)
	if !sdkcommon.FileExists(path) {
		return ""
	}
	contents, err := ioutil.ReadFile(path)
	if err != nil {
		logger.Debugf(ctx, "Error reading cached remote-host file %q: %s", path, err)
	}
	return strings.TrimSpace(string(contents))
}

func setCachedRemoteHost(ctx context.Context, dataPath string, remoteHost string) {
	if dataPath == "" {
		return
	}
	path := filepath.Join(dataPath, remoteHostFlag)
	if remoteHost == "" && sdkcommon.FileExists(path) {
		os.Remove(path)
		return
	}
	f, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0644)
	if err != nil {
		logger.Debugf(ctx, "Error opening remote-host cache file %q: %s", path, err)
	}
	defer f.Close()
	if _, err = f.Write([]byte(remoteHost)); err != nil {
		logger.Debugf(ctx, "Error writing remote-host cache file: %s", err)
	}
}

func validHostname(hostname string) bool {
	// Host name is of the forms
	//   somename.region.zone.doman.tld
	// use a regex to make sure each part is at least 1 character only separated by period
	var re = regexp.MustCompile(`(?m)^[\w-]*(?:\.\w+)+$`)
	return re.MatchString(hostname)
}

type intSlice []int

func (i *intSlice) String() string {
	result := make([]string, len(*i))
	for idx, val := range *i {
		result[idx] = strconv.Itoa(val)
	}

	return strings.Join(result, ",")
}

func (i *intSlice) Set(s string) error {
	for _, value := range strings.Split(s, ",") {
		tmp, err := strconv.Atoi(strings.TrimSpace(value))
		if err != nil {
			return err
		}
		*i = append(*i, tmp)
	}
	return nil
}

func usedPortsString() string {
	result := []string{}
	for key := range tunnel.UsedPorts {
		result = append(result, strconv.Itoa(key))
	}
	sort.Strings(result)
	return strings.Join(result, ", ")
}
