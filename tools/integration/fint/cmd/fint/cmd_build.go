// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"path/filepath"

	"github.com/google/subcommands"

	"go.fuchsia.dev/fuchsia/tools/integration/fint"
)

const (
	// buildArtifactsManifest is the name of the file (in
	// `contextSpec.ArtifactDir`) that will expose metadata produced by this
	// command to the caller. See setArtifactsManifest documentation for an
	// explanation of why we use JSON instead of textproto for this file.
	buildArtifactsManifest = "build_artifacts.json"
)

type BuildCommand struct {
	BaseCommand
}

func (*BuildCommand) Name() string { return "build" }

func (*BuildCommand) Synopsis() string {
	return "runs ninja with targets based on the input specs"
}

func (*BuildCommand) Usage() string {
	return `fint build -static <path> [-context <path>]

flags:
`
}

func (c *BuildCommand) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	return c.execute(ctx, func(ctx context.Context) error {
		staticSpec, contextSpec, err := c.loadSpecs()
		if err != nil {
			return err
		}

		artifacts, buildErr := fint.Build(ctx, staticSpec, contextSpec)
		if contextSpec.ArtifactDir != "" {
			path := filepath.Join(contextSpec.ArtifactDir, buildArtifactsManifest)
			if err := writeJSONPB(artifacts, path); err != nil {
				if buildErr != nil {
					return fmt.Errorf("%s (original error: %w)", err, buildErr)
				}
				return err
			}
		}
		return buildErr
	})
}
