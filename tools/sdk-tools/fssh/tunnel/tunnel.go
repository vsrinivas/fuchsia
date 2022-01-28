// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package tunnel

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"html/template"
	"os/exec"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	ffxAddRemoteTargetMessage = `To make your local device visible over the tunnel, run the following command on your remote workstation: ffx target add \"\[::1\]:8022\"`
	sshControlPath            = "~/.ssh/control-fuchsia-tunnel"
	maxCleanupAttempts        = 5

	// DebugLoggingSSHConfig is a string for adding debugging to the ssh config.
	DebugLoggingSSHConfig = `# Add additional log levels to be added to fssh's debug logs.
LogLevel DEBUG2`

	// DefaultSSHConfigTemplate is the contents of the default SSH
	// configuration file used by fssh if no other SSH config is specified.
	DefaultSSHConfigTemplate = `
Include /etc/ssh/ssh_config
# This SSH config is only used for the specified host.
Host {{.Remote}}
HostName {{.Remote}}
# We want ipv6 binds for the port forwards
AddressFamily inet6
# This config sets the ControlPath to a value specific to this tool.
# This prevents collisions with other SSH connections and so this tool
# can manage a connection seperately from the user's connection. We
# intentionally do not use %h/%p in the control path because
# there can only be one forwarding session at a time (due to the local
# forward of 8083).
ControlPath {{.TunnelPath}}
# "ControlMaster auto" enables multiple SSH connections to use the same
# tunnel/TCP connection. Enabling this option means that SSH connections
# can be multiplexed over the initial SSH connection created by this
# tool. The 'auto' value means that the initial connction will be
# created as the "master" connection if no connection is found based
# on the control path. Subsuquent sessions will use the initial
# connection.
ControlMaster auto
# Disable pseudo-tty allocation for screen based programs over the SSH tunnel.
RequestTTY no
# Request to a package server on the local host are forwarded to the remote
# host.
LocalForward *:8083 localhost:8083
# Requests from the remote to ssh to localhost:8022 will be forwarded to the
# target.
RemoteForward 8022 [{{.DeviceIP}}]:22
# zxdb & fidlcat requests from the remote to 2345 are forwarded to the target.
RemoteForward 2345 [{{.DeviceIP}}]:2345
# libassistant debug requests from the remote to 8007 are forwarded to the
# target.
RemoteForward 8007 [{{.DeviceIP}}]:8007
# CasAgent setup server requests on ports 8008 and 8443 on the remote are
# forwarded to the corresponding target port.
RemoteForward 8008 [{{.DeviceIP}}]:8008
RemoteForward 8443 [{{.DeviceIP}}]:8443
# SL4F requests to port 9080 on the remote are forwarded to target port 80.
RemoteForward 9080 [{{.DeviceIP}}]:80
# UMA log requests to port 8888 on the remote are forwarded to target port 8888.
RemoteForward 8888 [{{.DeviceIP}}]:8888
{{range .TunnelPorts}}
RemoteForward {{.}} [{{$.DeviceIP}}]:{{.}}
{{end}}
# Tear down the connection if port forwarding fails.
ExitOnForwardFailure yes
# Keep a blank line at the end.


`
)

var (
	attemptCount = 0

	// UsedPorts is a set of port numbers which are used by the default SSH configuration.
	UsedPorts = map[int]struct{}{
		2345: {},
		8007: {},
		8008: {},
		8022: {},
		8083: {},
		8443: {},
		8888: {},
		9080: {},
	}

	// ExecCommand exposes exec.Command as a variable so it can be mocked.
	ExecCommand = exec.Command
)

// Cmd creates a command to create a tunnel between a remote and local device
// for the purposes of building and developing Fuchsia packages.
//
// Example bash command equivalent:
//   ```
//   ssh \
//   -S ~/.ssh/control-fuchsia-tunnel
//   -o ControlMaster=auto
//   -t -M -t -6
//   -L \*:8083:localhost:8083
//   -R 8022:[192.168.1.1]:22
//   -R 2345:[192.168.1.1]:2345
//   -R 8443:[192.168.1.1]:8443
//   -R 9080:[192.168.1.1]:80
//   -R 8888:[192.168.1.1]:8888
//   -o ExitOnForwardFailure=yes
//   matthewcarroll@matt.c.googlers.com
//   ```
func Cmd(sshPath string, sshConfigPath string, remote string) (*exec.Cmd, error) {
	if sshPath == "" {
		var err error
		sshPath, err = findSSH()
		if err != nil {
			return &exec.Cmd{}, err
		}
	}

	args := []string{
		// specify which SSH config file to use.
		"-F",
		sshConfigPath,
		remote,
		// Ensure the connection doesn't exit because there is no traffic.
		"-n",
		"echo",
		"Tunnel is established",
		"&&",
		"echo",
		ffxAddRemoteTargetMessage,
		"&&",
		"sleep",
		"infinity",
	}
	return ExecCommand(sshPath, args...), nil
}

// CleanupTunnel cleans up the SSH tunnel by exiting the Multiplexed
// session.
// Returns string of cleanup steps, intended for testing
// error if one is encountered.
func CleanupTunnel(ctx context.Context, sshPath string, remote string) (string, error) {
	if sshPath == "" {
		var err error
		sshPath, err = findSSH()
		if err != nil {
			return "", err
		}
	}

	// Don't use the SSH config here, since we're trying to exit it.
	args := []string{
		remote,
		"-O",
		"exit",
		"-S",
		sshControlPath,
	}

	// Do any clean up on the remote side before exiting.
	// There is a chance of infinite recursion if the
	// sshd session on the remote is not killed, so
	// use a counter to keep the looping under maxCleanupAttempts.
	attemptCount = 0
	result := cleanupRemoteForwarding(ctx, sshPath, remote)

	// Use the mockable exec.command to run this command so it is testable.
	resultBytes, err := ExecCommand(sshPath, args...).CombinedOutput()
	result += string(resultBytes)
	// If the error is "Control socket connect...: No such file or directory" ignore it.
	if err != nil {
		if strings.Contains(result, "Control socket connect") && strings.Contains(result, "No such file or directory") {
			logger.Debugf(ctx, "Ignoring non-error cleaning up %v: %s", err, result)
			result = ""
			err = nil
		}
	}
	return result, err
}

// cleanupRemoteForwarding checks the remote host for a process listening on
// the SSH port forwarded to the device. If found, we speculatively attempt to clean
// it up.
//
// Returns output string for testing, or error if ssh cannot be found.
func cleanupRemoteForwarding(ctx context.Context, sshPath string, remote string) string {
	var (
		err    error
		result string
	)

	if sshPath == "" {
		sshPath, err = findSSH()
		if err != nil {
			logger.Warningf(ctx, "Cannot find ssh: %v", err)
			return ""
		}
	}

	if isTunnelAlreadyOpen(ctx, sshPath, remote) {
		result = fmt.Sprintf("Existing port forwarding found on %s, Cleaning up sshd sessions remotely.\n",
			remote)
		cleanupRemoteSSHd(ctx, sshPath, remote)
	}
	return result
}

func isTunnelAlreadyOpen(ctx context.Context, sshPath string, remote string) bool {
	var exitError *exec.ExitError
	// Look for 8022 being in LISTENING state.
	args := []string{
		remote,
		"ss -ln | grep :8022",
	}
	output, err := ExecCommand(sshPath, args...).Output()
	if err != nil {
		if errors.As(err, &exitError) {
			if len(exitError.Stderr) > 0 {
				message := string(exitError.Stderr)
				logger.Errorf(ctx, "%v returned %v: %s", args, exitError, message)
				return false
			}
		} else {
			logger.Debugf(ctx, "%v returned %v", args, err)
			return false
		}
	}
	if len(output) > 0 {
		logger.Debugf(ctx, "Found existing forwarding on %s: %s", remote, string(output))
		return true
	}
	return false
}

// Cleanup the remote sshd processes.
// These processes are started by SSHd to listen for the forwarded port.
// Since they are started by the system, the actual PID of processes
// is not reported by ss or other network tools without using sudo,
// which we want to avoid using.
//
// The next best is to look for sshd sessions not using tty. This avoids
// killing any interactive sessions that also exist.
//
// Returns output string for test inspection.
func cleanupRemoteSSHd(ctx context.Context, sshPath string, remote string) string {
	var exitError *exec.ExitError

	// Get the sshd processes that do not have a tty.
	args := []string{
		remote,
		"ps `pgrep -u $USER sshd` | grep notty",
	}
	output, err := ExecCommand(sshPath, args...).Output()
	if err != nil {
		if errors.As(err, &exitError) {
			if len(exitError.Stderr) > 0 {
				message := string(exitError.Stderr)
				logger.Debugf(ctx, "%v returned %v: %s", args, exitError, message)
				return fmt.Sprintf("%v: %s", exitError, message)
			}
		} else {
			logger.Debugf(ctx, "%v returned %v", args, err)
			return ""
		}
	}

	// Split the output into lines, ignoring blank lines.
	nonEmptyLineSplitter := func(c rune) bool {
		return c == '\n'
	}

	// There can be sshd instances that are notty and not handling
	// the tunnel. so kill 1 sshd instance and then recurse.
	// This is a little slower than killing all of them, but
	// is less disruptive to other ssh instances such as vscode.
	lines := strings.FieldsFunc(string(output), nonEmptyLineSplitter)
	logger.Debugf(ctx, "ps respose is %v", lines)
	if len(lines) != 0 {
		fields := strings.Fields(lines[0])
		if fields[0] != "" {
			args = []string{
				remote,
				"kill",
				"-9",
				fields[0],
			}
			output, err := ExecCommand(sshPath, args...).Output()
			if err != nil {
				if errors.As(err, &exitError) {
					if len(exitError.Stderr) > 0 {
						message := string(exitError.Stderr)
						logger.Warningf(ctx, "%v returned %v: %v", args, exitError, message)
					}
				} else {
					logger.Warningf(ctx, "%v returned %v", args, err)
				}
			}
			logger.Debugf(ctx, "%v returned %v", args, output)
			// Since we are recursing based on the state of the remote computer,
			// there is no way to guarantee the recursion is finite, so limit
			// the looping to maxCleanupAttempts.
			if isTunnelAlreadyOpen(ctx, sshPath, remote) {
				if attemptCount < maxCleanupAttempts {
					attemptCount++
					return cleanupRemoteForwarding(ctx, sshPath, remote)
				} else {
					logger.Warningf(ctx, "Attempted to cleanup existing tunnel %d times without success. Giving up.", maxCleanupAttempts)
				}
			}
		}
	}
	return ""
}

// findSSH finds a executable with the name `ssh` in the directories specified
// by the PATH environment variable.
func findSSH() (string, error) {
	path, err := exec.LookPath("ssh")
	if err != nil {
		return "", err
	}
	return path, nil
}

// GenerateSSHConfig generates a default SSH config file based on the
// specified template `tpl`, `remote`, `deviceIP`, and `tunnelPorts`.
func GenerateSSHConfig(tpl string, remote string, deviceIP string, tunnelPorts []int, verbose bool) ([]byte, error) {
	filteredTunnelPorts := []int{}
	badTunnelPorts := []string{}
	for _, port := range tunnelPorts {
		if port < 1024 {
			badTunnelPorts = append(badTunnelPorts, strconv.Itoa(port))
			continue
		}
		if _, used := UsedPorts[port]; !used {
			filteredTunnelPorts = append(filteredTunnelPorts, port)
		}
	}
	if len(badTunnelPorts) > 0 {
		return []byte{}, fmt.Errorf("Cannot create SSH config with protected ports: %s", strings.Join(badTunnelPorts, ", "))
	}

	data := struct {
		Remote      string
		DeviceIP    string
		TunnelPath  string
		TunnelPorts []int
	}{
		remote,
		deviceIP,
		sshControlPath,
		filteredTunnelPorts,
	}
	tmpl, err := template.New("sshconfig").Parse(tpl)
	if err != nil {
		err = fmt.Errorf("Could not create SSH config template: %v", err)
		return []byte{}, err
	}
	buf := &bytes.Buffer{}
	if err = tmpl.Execute(buf, data); err != nil {
		err = fmt.Errorf("Could not execute SSH config template: %v", err)
		return []byte{}, err
	}
	sshConfig := buf.Bytes()
	if len(sshConfig) < 1 {
		return sshConfig, fmt.Errorf("empty SSH config generated")
	}
	if verbose {
		sshConfig = append(sshConfig, []byte(DebugLoggingSSHConfig)...)
	}
	return sshConfig, nil
}
