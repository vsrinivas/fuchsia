// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This tool returns the "Last Known Good Build" for a given Buildbucket builder.
// Builder ids are often duplicated across Buildbucket buckets, so the id must be
// fully qualified using the project and bucket.
//
// Example Usage:
//
// $ lkgb fuchsia/ci/fuchsia-x64-release
// $ lkgb fuchsia/try/fuchsia-x64-debug
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"

	"fuchsia.googlesource.com/tools/buildbucket"
	"go.chromium.org/luci/auth/client/authcli"
	"go.chromium.org/luci/hardcoded/chromeinfra"
	"google.golang.org/genproto/protobuf/field_mask"

	buildbucketpb "go.chromium.org/luci/buildbucket/proto"
)

// Command line flags
var (
	// LUCI authentication flags.
	authFlags authcli.Flags

	// A flag for reading a BuildBucket Builder ID from command line args.
	builderIDFlag = buildbucket.BuilderID()

	// Whether to show help and exit.
	help bool
)

func usage() {
	fmt.Printf(`lkgb [flags] builder-id
lkgb returns the "Last Known Good Build" id for a given builder.  builder-id must have the
form "project/bucket/builder". For example: "fuchsia/ci/fuchsia-x64-release".
`)
	flag.PrintDefaults()
}

func init() {
	authFlags.Register(flag.CommandLine, chromeinfra.DefaultAuthOptions())
	flag.BoolVar(&help, "h", false, "Whether to show usage and exit")
	flag.Usage = usage
}

func main() {
	flag.Parse()
	if help || flag.NArg() == 0 {
		flag.Usage()
		return
	}

	if err := execute(context.Background(), flag.Arg(0)); err != nil {
		log.Println(err)
		os.Exit(1)
	}
	os.Exit(0)
}

func execute(ctx context.Context, input string) error {
	opts, err := authFlags.Options()
	if err != nil {
		return fmt.Errorf("failed to get auth options: %v", err)
	}

	if err := builderIDFlag.Set(input); err != nil {
		return fmt.Errorf("invalid builder id %q: %v", input, err)
	}

	client, err := buildbucket.NewBuildsClient(ctx, buildbucket.DefaultHost, opts)
	if err != nil {
		return fmt.Errorf("failed to create builds client: %v", err)
	}

	builderID := builderIDFlag.Get().(buildbucketpb.BuilderID)
	buildID, err := getBuildID(ctx, client, builderID)
	if err != nil {
		return fmt.Errorf("failed to get build: %v", err)
	}

	fmt.Printf("%d\n", buildID)
	return nil
}

func getBuildID(ctx context.Context, client buildbucketpb.BuildsClient, builderID buildbucketpb.BuilderID) (int64, error) {
	response, err := client.SearchBuilds(ctx, &buildbucketpb.SearchBuildsRequest{
		Predicate: &buildbucketpb.BuildPredicate{
			Builder: &builderID,
			Status:  buildbucketpb.Status_SUCCESS,
		},
		Fields: &field_mask.FieldMask{
			Paths: []string{"builds.*.id"},
		},
		PageSize: 1,
	})

	if err != nil {
		return 0, fmt.Errorf("search failed: %v", err)
	}
	if response.Builds == nil || len(response.Builds) == 0 {
		return 0, errors.New("no build found")
	}

	return response.Builds[0].Id, nil
}
