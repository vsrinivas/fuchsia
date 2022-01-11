// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"fmt"
	"io"
	"time"

	"github.com/golang/glog"
)

// An Instance is a specific combination of build, connector, and launcher,
// representable by a handle.  Most methods of this interface map directly to
// the ClusterFuchsia API.
type Instance interface {
	Start() error
	Stop() error
	ListFuzzers() []string
	Get(fuzzerName, targetSrc, hostDst string) error
	Put(fuzzerName, hostSrc, targetDst string) error
	RunFuzzer(out io.Writer, name, hostArtifactDir string, args ...string) error
	GetLogs(out io.Writer) error
	PrepareFuzzer(name string) error
	Handle() (Handle, error)
	Close()
}

// BaseInstance groups the core subobjects, to which most work will be delegated
type BaseInstance struct {
	Build     Build
	Connector Connector
	Launcher  Launcher

	// While we rewrite the handle data each time Handle() is called, we need to keep a
	// reference around to avoid recreating new Handles each time
	handle Handle

	// This will take a default value, and only needs to be overridden for tests
	reconnectInterval time.Duration
}

const defaultReconnectInterval = 3 * time.Second
const maxInitialConnectAttempts = 5

// NewInstance creates a fresh instance
func NewInstance() (Instance, error) {
	build, err := NewBuild()
	if err != nil {
		return nil, fmt.Errorf("Error configuring build: %s", err)
	}

	if err := build.Prepare(); err != nil {
		return nil, fmt.Errorf("Error preparing build: %s", err)
	}

	// TODO(fxbug.dev/47479): should the user be able to choose connector/launcher types?
	launcher := NewQemuLauncher(build)

	// Note: We can't get a Connector until the Launcher has started
	return &BaseInstance{build, nil, launcher, "", defaultReconnectInterval}, nil
}

func loadInstanceFromHandle(handle Handle) (Instance, error) {
	// TODO(fxbug.dev/47320): Store build info in the handle too
	build, err := NewBuild()
	if err != nil {
		return nil, fmt.Errorf("Error configuring build: %s", err)
	}

	connector, err := loadConnectorFromHandle(handle)
	if err != nil {
		return nil, err
	}

	launcher, err := loadLauncherFromHandle(build, handle)
	if err != nil {
		return nil, err
	}

	return &BaseInstance{build, connector, launcher, handle, defaultReconnectInterval}, nil
}

// Close releases the Instance, but doesn't Stop it
func (i *BaseInstance) Close() {
	i.Connector.Close()
}

// Start boots up the instance and waits for connectivity to be established
//
// If Start succeeds, it is up to the caller to clean up by calling Stop later.
// However, if it fails, any resources will have been automatically released.
func (i *BaseInstance) Start() error {
	conn, err := i.Launcher.Start()
	if err != nil {
		return err
	}
	i.Connector = conn

	glog.Infof("Waiting for connectivity...")

	first := true
	for j := 1; j <= maxInitialConnectAttempts; j++ {
		if !first {
			// Check if instance still appears to be running; there is no point
			// in attempting to connect if it is down.
			running, err := i.Launcher.IsRunning()
			if err != nil {
				i.Launcher.Kill()
				return fmt.Errorf("Error checking instance status: %s", err)
			}
			if !running {
				i.Launcher.Kill()
				if qemuLauncher, ok := i.Launcher.(*QemuLauncher); ok {
					return fmt.Errorf("Instance (PID %d) not running", qemuLauncher.Pid)
				}
				return fmt.Errorf("Instance not running")
			}

			glog.Warningf("Retrying in %s...", i.reconnectInterval)
			time.Sleep(i.reconnectInterval)
		}
		first = false

		cmd := conn.Command("echo", "hello")
		cmd.SetTimeout(5 * time.Second)

		// Start() will return an error if we have trouble connecting to the server.
		// Note that the underlying connector may have its own additional retry logic.
		if err := cmd.Start(); err != nil {
			glog.Warningf("Connection failed during attempt %d: %s", j, err)
			continue
		}

		// Wait() will return an error if we have trouble executing the remote
		// command. Some example cases would be:
		// - Timeout running the command
		// - If you forgot to --with-base the devtools:
		// error: 2 (/boot/bin/sh: 1: Cannot create child process: -1 (ZX_ERR_INTERNAL):
		// failed to resolve fuchsia-pkg://fuchsia.com/ls#bin/ls
		if err := cmd.Wait(); err != nil {
			glog.Warningf("Wait failed during attempt %d: %s", j, err)
			continue
		}

		glog.Info("Instance is now online.")

		if sc, ok := conn.(*SSHConnector); ok {
			glog.Infof("Access via: ssh -i %q -p %d %s", sc.Key, sc.Port, sc.Host)
		}

		return nil
	}

	i.Launcher.Kill()
	return fmt.Errorf("error establishing connectivity to instance")
}

// RunFuzzer runs the named fuzzer on the Instance. If `hostArtifactDir` is
// specified and the run generated any output artifacts, they will be copied to
// that directory. `args` is an optional list of arguments in the form
// `-key=value` that will be passed to libFuzzer.
func (i *BaseInstance) RunFuzzer(out io.Writer, name, hostArtifactDir string, args ...string) error {
	fuzzer, err := i.Build.Fuzzer(name)
	if err != nil {
		return err
	}

	fuzzer.Parse(args)
	artifacts, err := fuzzer.Run(i.Connector, out, hostArtifactDir)
	if err != nil {
		return err
	}

	if hostArtifactDir != "" {
		for _, art := range artifacts {
			if err := i.Get(name, art, hostArtifactDir); err != nil {
				return err
			}
		}
	}

	return nil
}

// PrepareFuzzer ensures the named fuzzer is ready to be used on the Instance.
// This must be called before running the fuzzer or exchanging any data with
// the fuzzer. If called for a fuzzer that has already been previously
// prepared, it will reset the state of that fuzzer: clearing data directories,
// etc.
//
// This method is explicitly separated from RunFuzzer and others to ensure that
// any caller timeouts being enforced on fuzzer command execution are not
// affected by unrelated setup delays such as package fetching over the network.
func (i *BaseInstance) PrepareFuzzer(name string) error {
	fuzzer, err := i.Build.Fuzzer(name)
	if err != nil {
		return err
	}

	if err := fuzzer.Prepare(i.Connector); err != nil {
		return err
	}

	return nil
}

// GetLogs writes any system logs for an instance to `out`
func (i *BaseInstance) GetLogs(out io.Writer) error {
	return i.Launcher.GetLogs(out)
}

// Get copies files from a fuzzer namespace on the Instance to the host
func (i *BaseInstance) Get(fuzzerName, targetSrc, hostDst string) error {
	fuzzer, err := i.Build.Fuzzer(fuzzerName)
	if err != nil {
		return err
	}
	return i.Connector.Get(fuzzer.AbsPath(targetSrc), hostDst)
}

// Put copies files from the host to a fuzzer namespace on the Instance
func (i *BaseInstance) Put(fuzzerName, hostSrc, targetDst string) error {
	fuzzer, err := i.Build.Fuzzer(fuzzerName)
	if err != nil {
		return err
	}
	return i.Connector.Put(hostSrc, fuzzer.AbsPath(targetDst))
}

// Stop shuts down the Instance
func (i *BaseInstance) Stop() error {
	if err := i.Launcher.Kill(); err != nil {
		return err
	}

	if i.handle != "" {
		i.handle.Release()
	}

	return nil
}

// Handle returns a Handle representing the Instance
func (i *BaseInstance) Handle() (Handle, error) {
	data := HandleData{i.Connector, i.Launcher}

	if i.handle == "" {
		// Create a new handle from scratch
		handle, err := NewHandleWithData(data)

		if err != nil {
			return "", fmt.Errorf("error constructing instance handle: %s", err)
		}

		i.handle = handle
		return handle, nil
	}

	// Update an existing handle, but don't release it even if we fail
	if err := i.handle.SetData(data); err != nil {
		return "", fmt.Errorf("error updating instance handle: %s", err)
	}

	return i.handle, nil
}

// ListFuzzers lists fuzzers available on the Instance
func (i *BaseInstance) ListFuzzers() []string {
	return i.Build.ListFuzzers()
}
