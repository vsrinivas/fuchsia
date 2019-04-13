// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"archive/tar"
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"strings"
	"time"

	"fuchsia.googlesource.com/tools/tarutil"
	"fuchsia.googlesource.com/tools/botanist/target"
	"fuchsia.googlesource.com/tools/build"
	"fuchsia.googlesource.com/tools/command"
	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/netutil"
	"fuchsia.googlesource.com/tools/runner"
	"fuchsia.googlesource.com/tools/runtests"

	"github.com/google/subcommands"
)

// ZedbootCommand is a Command implementation for running the testing workflow on a device
// that boots with Zedboot.
type ZedbootCommand struct {
	// ImageManifests is a list of paths to image manifests (e.g., images.json)
	imageManifests command.StringsFlag

	// Netboot tells botanist to netboot (and not to pave).
	netboot bool

	// ConfigFile is the path to a file containing the target config.
	configFile string

	// TestResultsDir is the directory on target to where test results will be written.
	testResultsDir string

	// SummaryFilename is the name of the test summary JSON file to be written to
	// testResultsDir.
	summaryFilename string

	// FilePollInterval is the duration waited between checking for test summary file
	// on the target to be written.
	filePollInterval time.Duration

	// OutputArchive is a path on host to where the tarball containing the test results
	// will be output.
	outputArchive string

	// CmdlineFile is the path to a file of additional kernel command-line arguments.
	cmdlineFile string

	// Host command to run after paving device
	// TODO(IN-831): Remove when host-target-interaction infra is ready
	hostCmd string
}

func (*ZedbootCommand) Name() string {
	return "zedboot"
}

func (*ZedbootCommand) Usage() string {
	return "zedboot [flags...] [kernel command-line arguments...]\n\nflags:\n"
}

func (*ZedbootCommand) Synopsis() string {
	return "boots a Zedboot device and collects test results"
}

func (cmd *ZedbootCommand) SetFlags(f *flag.FlagSet) {
	f.Var(&cmd.imageManifests, "images", "paths to image manifests")
	f.BoolVar(&cmd.netboot, "netboot", false, "if set, botanist will not pave; but will netboot instead")
	f.StringVar(&cmd.testResultsDir, "results-dir", "/test", "path on target to where test results will be written")
	f.StringVar(&cmd.outputArchive, "out", "output.tar", "path on host to output tarball of test results")
	f.StringVar(&cmd.summaryFilename, "summary-name", runtests.TestSummaryFilename, "name of the file in the test directory")
	f.DurationVar(&cmd.filePollInterval, "poll-interval", 1*time.Minute, "time between checking for summary.json on the target")
	f.StringVar(&cmd.configFile, "config", "/etc/botanist/config.json", "path to file of device config")
	f.StringVar(&cmd.cmdlineFile, "cmdline-file", "", "path to a file containing additional kernel command-line arguments")
	f.StringVar(&cmd.hostCmd, "hacky-host-cmd", "", "host command to run after paving. To be removed on completion of IN-831")
}

// Creates and returns Summary file object for Host Cmds.
func (cmd *ZedbootCommand) hostSummaryJSON(ctx context.Context, err error) (*bytes.Buffer, error) {
	var cmdResult runtests.TestResult

	if err != nil {
		cmdResult = runtests.TestFailure
		logger.Infof(ctx, "Command failed! %v\n", err)
	} else {
		cmdResult = runtests.TestSuccess
		logger.Infof(ctx, "Command succeeded!\n")
	}

	// Create coarse-grained summary based on host command exit code
	testDetail := runtests.TestDetails{
		Name:       cmd.hostCmd,
		OutputFile: runtests.TestOutputFilename,
		Result:     cmdResult,
	}

	result := runtests.TestSummary{
		Tests: []runtests.TestDetails{testDetail},
	}

	b, err := json.Marshal(result)
	if err != nil {
		return nil, err
	}
	buffer := bytes.NewBuffer(b)

	return buffer, nil
}

// Creates tar archive from host command artifacts.
func (cmd *ZedbootCommand) tarHostCmdArtifacts(summary []byte, cmdOutput []byte, outputDir string) error {
	outFile, err := os.OpenFile(cmd.outputArchive, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return fmt.Errorf("failed to create file %s: %v", cmd.outputArchive, err)
	}

	tw := tar.NewWriter(outFile)
	defer tw.Close()

	// Write summary to archive
	if err = tarutil.TarBuffer(tw, summary, cmd.summaryFilename); err != nil {
		return err
	}

	// Write combined stdout & stderr output to archive
	if err = tarutil.TarBuffer(tw, cmdOutput, runtests.TestOutputFilename); err != nil {
		return err
	}

	// Write all output files from the host cmd to the archive.
	return tarutil.TarDirectory(tw, outputDir)
}

// Executes host command and creates result tar from command output
func (cmd *ZedbootCommand) runHostCmd(ctx context.Context) error {
	// Create tmp directory to run host command out of
	tmpDir, err := ioutil.TempDir("", "output")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir)

	// Define multiwriters so cmd outputs to both stdout/stderr and respective buffers
	// This allows users to see output on the fly while also storing results
	var stdoutBuf, stderrBuf bytes.Buffer
	stdout := io.MultiWriter(os.Stdout, &stdoutBuf)
	stderr := io.MultiWriter(os.Stdout, &stderrBuf)

	ctx, cancel := context.WithTimeout(ctx, 60*time.Minute)
	defer cancel()
	logger.Debugf(ctx, "executing command: %q\n", cmd.hostCmd)
	runner := runner.SubprocessRunner{
		Dir: tmpDir,
	}
	hostCmdErr := runner.Run(ctx, []string{cmd.hostCmd}, stdout, stderr)

	// Create summary JSON based on host command exit code.
	summaryBuffer, err := cmd.hostSummaryJSON(ctx, hostCmdErr)
	if err != nil {
		return err
	}

	stdoutBuf.Write(stderrBuf.Bytes())
	return cmd.tarHostCmdArtifacts(summaryBuffer.Bytes(), stdoutBuf.Bytes(), tmpDir)
}

func (cmd *ZedbootCommand) runTests(ctx context.Context, imgs build.Images, addr *net.UDPAddr, cmdlineArgs []string) error {
	// Handle host commands
	// TODO(IN-831): Remove when host-target-interaction infra is ready
	if cmd.hostCmd != "" {
		return cmd.runHostCmd(ctx)
	}

	logger.Debugf(ctx, "waiting for %q\n", cmd.summaryFilename)
	return runtests.PollForSummary(ctx, addr, cmd.summaryFilename, cmd.testResultsDir, cmd.outputArchive, cmd.filePollInterval)
}

func (cmd *ZedbootCommand) execute(ctx context.Context, cmdlineArgs []string) error {
	configs, err := target.LoadDeviceConfigs(cmd.configFile)

	if err != nil {
		return fmt.Errorf("failed to load target config file %q", cmd.configFile)
	}
	opts := target.Options{
		Netboot:  cmd.netboot,
	}

	var devices []*target.DeviceTarget
	for _, config := range configs {
		device, err := target.NewDeviceTarget(ctx, config, opts)
		if err != nil {
			return err
		}
		devices = append(devices, device)
	}

	for _, device := range devices {
		defer device.Restart(ctx)
	}

	imgs, err := build.LoadImages(cmd.imageManifests...)
	if err != nil {
		return err
	}

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	errs := make(chan error)

	for _, device := range devices {
		go func(device *target.DeviceTarget) {
			if err := device.Start(ctx, imgs, cmdlineArgs); err != nil {
				errs <- err
			}
		}(device)
	}
	go func() {
		addr, err := netutil.GetNodeAddress(ctx, devices[0].Nodename(), false)
		if err != nil {
			errs <- err
			return
		}
		errs <- cmd.runTests(ctx, imgs, addr, cmdlineArgs)
	}()

	select {
	case err := <-errs:
		return err
	case <-ctx.Done():
	}

	return nil
}

func (cmd *ZedbootCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	configFlag := f.Lookup("config")
	logger.Debugf(ctx, "config flag: %v\n", configFlag.Value)

	// Aggregate command-line arguments.
	cmdlineArgs := f.Args()
	if cmd.cmdlineFile != "" {
		args, err := ioutil.ReadFile(cmd.cmdlineFile)
		if err != nil {
			logger.Errorf(ctx, "failed to read command-line args file %q: %v\n", cmd.cmdlineFile, err)
			return subcommands.ExitFailure
		}
		cmdlineArgs = append(cmdlineArgs, strings.Split(string(args), "\n")...)
	}

	if err := cmd.execute(ctx, cmdlineArgs); err != nil {
		logger.Errorf(ctx, "%v\n", err)
		return subcommands.ExitFailure
	}

	return subcommands.ExitSuccess
}
