// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package ffxutil provides support for running ffx commands.
package ffxutil

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil/constants"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
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

// FFXInstance takes in a path to the ffx tool and runs ffx commands with the provided config.
type FFXInstance struct {
	ffxPath string

	// Config represents the config associated with this ffx instance.
	Config *FFXConfig
	// ConfigPath is the path to the ffx config.
	ConfigPath string

	runner *subprocess.Runner
	stdout io.Writer
	stderr io.Writer
	target string
}

// NewFFXInstance creates an isolated FFXInstance.
func NewFFXInstance(ffxPath string, dir string, env []string, target, sshKey string, outputDir string) (*FFXInstance, error) {
	if ffxPath == "" {
		return nil, nil
	}
	config := newIsolatedFFXConfig(outputDir)
	config.Set("target", map[string]string{"default": target})
	sshKey, err := filepath.Abs(sshKey)
	if err != nil {
		config.Close()
		return nil, err
	}
	config.Set("ssh", map[string][]string{"priv": {sshKey}})
	config.Set("test", map[string]bool{
		"experimental_structured_output": true,
		"experimental_json_input":        true,
	})
	configPath := filepath.Join(outputDir, "ffx_config.json")
	if err := config.ToFile(configPath); err != nil {
		config.Close()
		return nil, err
	}
	env = append(env, config.Env()...)
	ffx := FFXInstanceWithConfig(ffxPath, dir, env, target, configPath)
	if ffx != nil {
		ffx.Config = config
	}
	return ffx, nil
}

func FFXInstanceWithConfig(ffxPath, dir string, env []string, target, configPath string) *FFXInstance {
	if ffxPath == "" {
		return nil
	}
	return &FFXInstance{
		ffxPath:    ffxPath,
		ConfigPath: configPath,
		runner:     &subprocess.Runner{Dir: dir, Env: append(os.Environ(), env...)},
		stdout:     os.Stdout,
		stderr:     os.Stderr,
		target:     target,
	}
}

func (f *FFXInstance) SetTarget(target string) {
	f.target = target
}

// SetStdoutStderr sets the stdout and stderr for the ffx commands to write to.
func (f *FFXInstance) SetStdoutStderr(stdout, stderr io.Writer) {
	f.stdout = stdout
	f.stderr = stderr
}

// Run runs ffx with the associated config and provided args.
func (f *FFXInstance) Run(ctx context.Context, args ...string) error {
	args = append([]string{f.ffxPath, "--config", f.ConfigPath}, args...)
	if err := runCommand(ctx, f.runner, f.stdout, f.stderr, args...); err != nil {
		return fmt.Errorf("%s: %w", constants.CommandFailedMsg, err)
	}
	return nil
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
	if f.Config != nil {
		if configErr := f.Config.Close(); err == nil {
			err = configErr
		}
	}
	return err
}

// BootloaderBoot RAM boots the target.
func (f *FFXInstance) BootloaderBoot(ctx context.Context, zbi, vbmeta, slot string) error {
	var args []string
	if zbi != "" {
		args = append(args, "--zbi", zbi)
	}
	if vbmeta != "" {
		args = append(args, "--vbmeta", vbmeta)
	}
	if slot != "" {
		args = append(args, "--slot", slot)
	}
	return f.RunWithTarget(ctx, append([]string{"target", "bootloader", "boot"}, args...)...)
}

// Flash flashes the target.
func (f *FFXInstance) Flash(ctx context.Context, manifest, sshKey string) error {
	return f.RunWithTarget(ctx, "target", "flash", "--ssh-key", sshKey, manifest)
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
func (f *FFXInstance) Test(ctx context.Context, tests []TestDef, outDir string, args ...string) (*TestRunResult, error) {
	// Write the test def to a file and store in the outDir to upload with the test outputs.
	if err := os.MkdirAll(outDir, os.ModePerm); err != nil {
		return nil, err
	}
	testFile := filepath.Join(outDir, "test-file.json")
	if err := jsonutil.WriteToFile(testFile, tests); err != nil {
		return nil, err
	}
	// Create a new subdirectory within outDir to pass to --output-directory which is expected to be empty.
	testOutputDir := filepath.Join(outDir, "test-outputs")
	f.RunWithTarget(ctx, append([]string{"test", "run", "--continue-on-timeout", "--test-file", testFile, "--output-directory", testOutputDir}, args...)...)

	return getRunResult(testOutputDir)
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
