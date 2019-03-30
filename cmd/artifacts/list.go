// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"

	"fuchsia.googlesource.com/tools/artifacts"
	"fuchsia.googlesource.com/tools/buildbucket"
	"github.com/google/subcommands"
	"go.chromium.org/luci/auth/client/authcli"
	"go.chromium.org/luci/hardcoded/chromeinfra"
)

type ListCommand struct {
	build     string
	authFlags authcli.Flags
}

func (*ListCommand) Name() string {
	return "ls"
}

func (*ListCommand) Usage() string {
	return "ls [flags]"
}

func (*ListCommand) Synopsis() string {
	return "lists the set of artifacts produced by a build"
}

func (cmd *ListCommand) SetFlags(f *flag.FlagSet) {
	cmd.authFlags.Register(flag.CommandLine, chromeinfra.DefaultAuthOptions())
	f.StringVar(&cmd.build, "build", "", "the ID of the build that produced the artifacts")
}

func (cmd *ListCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := cmd.validateAndExecute(ctx); err != nil {
		log.Println(err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (cmd *ListCommand) validateAndExecute(ctx context.Context) error {
	opts, err := cmd.authFlags.Options()
	if err != nil {
		return err
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

func (cmd *ListCommand) execute(ctx context.Context, buildsCli buildsClient, artifactsCli *artifacts.Client) error {
	bucket, err := getStorageBucket(ctx, buildsCli, cmd.build)
	if err != nil {
		return err
	}

	dir := artifactsCli.GetBuildDir(bucket, cmd.build)
	items, err := dir.List(ctx)
	if err != nil {
		return err
	}

	for _, item := range items {
		fmt.Println(item)
	}

	return nil
}
