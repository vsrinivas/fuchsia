// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"runtime"

	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/tools/integration/cmd/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
)

type subprocessRunner interface {
	Run(ctx context.Context, cmd []string, stdout, stderr io.Writer) error
}

type SetCommand struct {
	staticSpecPath  string
	contextSpecPath string
}

func (*SetCommand) Name() string { return "set" }

func (*SetCommand) Synopsis() string { return "runs gn gen with args based on the input specs." }

func (*SetCommand) Usage() string {
	return `
fint set -context <path> -static <path>

flags:
`
}

func (c *SetCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&c.staticSpecPath, "static", "", "path to a Static .textproto")
	f.StringVar(&c.contextSpecPath, "context", "", "path to a Context .textproto")
}

func (c *SetCommand) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if c.staticSpecPath == "" || c.contextSpecPath == "" {
		logger.Errorf(ctx, "-static and -context flags are required")
		return subcommands.ExitUsageError
	}
	if err := c.run(ctx); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (c *SetCommand) run(ctx context.Context) error {
	logger.Debugf(ctx, "static spec path: %s", c.staticSpecPath)
	logger.Debugf(ctx, "context spec path: %s", c.contextSpecPath)

	bytes, err := ioutil.ReadFile(c.staticSpecPath)
	if err != nil {
		return err
	}

	if _, err = parseStatic(string(bytes)); err != nil {
		return err
	}

	bytes, err = ioutil.ReadFile(c.contextSpecPath)
	if err != nil {
		return err
	}

	contextSpec, err := parseContext(string(bytes))
	if err != nil {
		return err
	}

	platform, err := getPlatform()
	if err != nil {
		return err
	}

	runner := &runner.SubprocessRunner{}
	return runGen(ctx, runner, contextSpec, platform)
}

func runGen(ctx context.Context, runner subprocessRunner, contextSpec *proto.Context, platform string) error {
	gnPath := filepath.Join(contextSpec.CheckoutDir, "prebuilt", "third_party", "gn", platform, "gn")
	genCmd := []string{
		gnPath, "gen",
		contextSpec.BuildDir,
		"--check=system",
		"--fail-on-unused-args",
	}

	if err := runner.Run(ctx, genCmd, os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("error running gn gen: %w", err)
	}
	return nil
}

func getPlatform() (string, error) {
	os, ok := map[string]string{
		"windows": "win",
		"darwin":  "mac",
		"linux":   "linux",
	}[runtime.GOOS]
	if !ok {
		return "", fmt.Errorf("unsupported GOOS %q", runtime.GOOS)
	}

	arch, ok := map[string]string{
		"amd64": "x64",
		"arm64": "arm64",
	}[runtime.GOARCH]
	if !ok {
		return "", fmt.Errorf("unsupported GOARCH %q", runtime.GOARCH)
	}
	return fmt.Sprintf("%s-%s", os, arch), nil
}
