// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package ffxutil provides support for running ffx commands.
package ffxutil

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

const (
	// The name of the snapshot zip file that gets outputted by `ffx target snapshot`.
	// Keep in sync with //src/developer/ffx/plugins/target/snapshot/src/lib.rs.
	snapshotZipName = "snapshot.zip"
)

func runCommand(ctx context.Context, runner *subprocess.Runner, stdout, stderr io.Writer, args ...string) error {
	return runner.RunWithStdin(ctx, args, stdout, stderr, nil)
}

// FFXConfig describes a config to run ffx with.
type FFXConfig struct {
	socket string
	config map[string]interface{}
	env    []string
}

// NewIsolatedFFXConfig creates a config that provides an isolated environment to run ffx in.
func NewIsolatedFFXConfig(logDir, testOutputDir string) (*FFXConfig, error) {
	socketPath := filepath.Join(os.TempDir(), "ffx_socket")
	// TODO(fxbug.dev/64499): Stop setting environment variables once bug is fixed.
	// The env vars to set come from //src/developer/ffx/plugins/self-test/src/test/mod.rs.
	// TEMP, TMP, HOME, and XDG_CONFIG_HOME are set in //tools/lib/environment/environment.go
	// with a call to environment.Ensure().
	envMap := map[string]string{
		"ASCENDD": socketPath,
	}
	var env []string
	for k, v := range envMap {
		env = append(env, fmt.Sprintf("%s=%s", k, v))
	}
	config := &FFXConfig{
		socket: socketPath,
		config: make(map[string]interface{}),
		env:    env,
	}
	config.Set("overnet", map[string]string{"socket": socketPath})
	config.Set("log", map[string][]string{"dir": {logDir}})
	config.Set("test", map[string][]string{"output_path": {testOutputDir}})
	return config, nil
}

// Set sets values in the config.
func (c *FFXConfig) Set(key string, value interface{}) {
	c.config[key] = value
}

// ToFile writes the config to a file.
func (c *FFXConfig) ToFile(configPath string) error {
	bytes, err := json.MarshalIndent(c.config, "", "\t")
	if err != nil {
		return fmt.Errorf("failed to convert ffx config to bytes: %w", err)
	}
	configFile, err := osmisc.CreateFile(configPath)
	if err != nil {
		return fmt.Errorf("failed to create config file: %w", err)
	}
	defer configFile.Close()
	if _, err := configFile.Write(bytes); err != nil {
		return fmt.Errorf("failed to write ffx config to a file: %w", err)
	}
	return nil
}

// Close removes the socket if it hasn't been removed by `ffx daemon stop`.
func (c *FFXConfig) Close() error {
	if _, err := os.Stat(c.socket); !os.IsNotExist(err) {
		return os.Remove(c.socket)
	}
	return nil
}

// FFXInstance takes in a path to the ffx tool and runs ffx commands with the provided config.
type FFXInstance struct {
	ffxPath       string
	config        *FFXConfig
	configPath    string
	runner        *subprocess.Runner
	stdout        io.Writer
	stderr        io.Writer
	target        string
	testOutputDir string
}

// NewFFXInstance creates an isolated FFXInstance.
func NewFFXInstance(ffxPath string, dir string, env []string, target, sshKey string, outputDir string) (*FFXInstance, error) {
	if ffxPath == "" {
		return nil, nil
	}
	config, err := NewIsolatedFFXConfig(filepath.Join(outputDir, "logs"), filepath.Join(outputDir, "saved_test_runs"))
	if err != nil {
		return nil, err
	}
	config.Set("target", map[string]string{"default": target})
	sshKey, err = filepath.Abs(sshKey)
	if err != nil {
		config.Close()
		return nil, err
	}
	config.Set("ssh", map[string][]string{"priv": {sshKey}})
	config.Set("test", map[string]bool{"experimental_structured_output": true})
	configPath := filepath.Join(outputDir, "ffx_config.json")
	if err := config.ToFile(configPath); err != nil {
		config.Close()
		return nil, err
	}
	env = append(env, config.env...)
	ffx := &FFXInstance{
		ffxPath:       ffxPath,
		config:        config,
		configPath:    configPath,
		runner:        &subprocess.Runner{Dir: dir, Env: env},
		stdout:        os.Stdout,
		stderr:        os.Stderr,
		target:        target,
		testOutputDir: filepath.Join(outputDir, "test_outputs"),
	}
	return ffx, nil
}

// SetStdoutStderr sets the stdout and stderr for the ffx commands to write to.
func (f *FFXInstance) SetStdoutStderr(stdout, stderr io.Writer) {
	f.stdout = stdout
	f.stderr = stderr
}

// Run runs ffx with the associated config and provided args.
func (f *FFXInstance) Run(ctx context.Context, args ...string) error {
	args = append([]string{f.ffxPath, "--config", f.configPath}, args...)
	return runCommand(ctx, f.runner, f.stdout, f.stderr, args...)
}

// RunWithTarget runs ffx with the associated target.
func (f *FFXInstance) RunWithTarget(ctx context.Context, args ...string) error {
	args = append([]string{"--target", f.target}, args...)
	return f.Run(ctx, args...)
}

// Stop stops the daemon.
func (f *FFXInstance) Stop() error {
	// Use a new context for Stop() to give it time to complete.
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	err := f.Run(ctx, "daemon", "stop")
	if configErr := f.config.Close(); err == nil {
		err = configErr
	}
	return err
}

// List lists all available targets.
func (f *FFXInstance) List(ctx context.Context, args ...string) error {
	return f.Run(ctx, append([]string{"target", "list"}, args...)...)
}

// TargetWait waits until the target becomes available.
func (f *FFXInstance) TargetWait(ctx context.Context) error {
	return f.RunWithTarget(ctx, "target", "wait")
}

// Test runs a test suite.
func (f *FFXInstance) Test(ctx context.Context, test string, args ...string) error {
	return f.RunWithTarget(ctx, append([]string{"test", "run", test, "--output-directory", f.testOutputDir}, args...)...)
}

// Snapshot takes a snapshot of the target's state and saves it to outDir/snapshotFilename.
func (f *FFXInstance) Snapshot(ctx context.Context, outDir string, snapshotFilename string) error {
	err := f.RunWithTarget(ctx, "target", "snapshot", "--dir", outDir)
	if err != nil {
		return err
	}
	if snapshotFilename != "" && snapshotFilename != snapshotZipName {
		return os.Rename(filepath.Join(outDir, snapshotZipName), filepath.Join(outDir, snapshotFilename))
	}
	return nil
}

// GetConfig shows the ffx config.
func (f *FFXInstance) GetConfig(ctx context.Context) error {
	return f.Run(ctx, "config", "get")
}
