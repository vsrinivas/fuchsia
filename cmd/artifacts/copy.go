// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"os"

	"fuchsia.googlesource.com/tools/artifacts"
	"fuchsia.googlesource.com/tools/buildbucket"
	"github.com/google/subcommands"
	"go.chromium.org/luci/auth/client/authcli"
	"go.chromium.org/luci/hardcoded/chromeinfra"
)

type CopyCommand struct {
	authFlags authcli.Flags

	build string
	// The remote filepath with the target build's Cloud Storage directory.
	source string

	// The local path to write the artifact to.
	dest string
}

func (*CopyCommand) Name() string {
	return "cp"
}

func (*CopyCommand) Usage() string {
	return "cp [flags...]"
}

func (*CopyCommand) Synopsis() string {
	return "fetches an artifact produced by a Fuchsia builder"
}

func (cmd *CopyCommand) SetFlags(f *flag.FlagSet) {
	cmd.authFlags.Register(flag.CommandLine, chromeinfra.DefaultAuthOptions())
	f.StringVar(&cmd.build, "build", "", "the ID of the build that produced the artifacts")
	f.StringVar(&cmd.source, "src", "", "The artifact to download from the build's Cloud Storage directory")
	f.StringVar(&cmd.dest, "dst", "", "The local path to write the artifact to")
}

func (cmd *CopyCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := cmd.validateAndExecute(ctx); err != nil {
		log.Println(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (cmd *CopyCommand) validateAndExecute(ctx context.Context) error {
	opts, err := cmd.authFlags.Options()
	if err != nil {
		return err
	}

	if cmd.source == "" {
		return fmt.Errorf("missing -src")
	}

	if cmd.dest == "" {
		return fmt.Errorf("missing -dst")
	}

	buildsCli, err := buildbucket.NewBuildsClient(ctx, buildbucket.DefaultHost, opts)
	if err != nil {
		return fmt.Errorf("failed to create builds client: %v", err)
	}

	artifactsCli, err := artifacts.NewClient(ctx, opts)
	if err != nil {
		return fmt.Errorf("failed to create artifacts client: %v", err)
	}

	return cmd.execute(ctx, buildsCli, artifactsCli)
}

func (cmd *CopyCommand) execute(ctx context.Context, buildsCli buildsClient, artifactsCli *artifacts.Client) error {
	bucket, err := getStorageBucket(ctx, buildsCli, cmd.build)
	if err != nil {
		return err
	}

	dir := artifactsCli.GetBuildDir(bucket, cmd.build)
	obj := dir.Object(cmd.source)
	input, err := obj.NewReader(ctx)
	if err != nil {
		return err
	}

	output, err := os.Create(cmd.dest)
	if err != nil {
		return err
	}

	_, err = io.Copy(output, input)
	return err
}
