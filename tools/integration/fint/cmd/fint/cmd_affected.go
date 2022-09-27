// Copyright 2022 The Fuchsia Authors. All rights reserved.
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
	"google.golang.org/protobuf/proto"
)

type AffectedCommand struct {
	BaseCommand
}

func (*AffectedCommand) Name() string { return "affected" }

func (*AffectedCommand) Synopsis() string {
	return "runs ninja to determine if/what parts of the build graph were affected"
}

func (*AffectedCommand) Usage() string {
	return `fint affected -static <path> -context <path>

flags:
`
}

func (a *AffectedCommand) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	return a.execute(ctx, func(ctx context.Context) error {
		staticSpec, contextSpec, err := a.loadSpecs()
		if err != nil {
			return err
		}

		// The affected subcommand modifies the build artifacts manifest, so we
		// need to load the existing manifest and merge the new one into it.
		existingArtifacts, err := fint.ReadBuildArtifacts(buildArtifactsManifest)
		if err != nil {
			return err
		}
		artifacts, affectedErr := fint.Affected(ctx, staticSpec, contextSpec)
		proto.Merge(artifacts, existingArtifacts)

		if contextSpec.ArtifactDir != "" {
			path := filepath.Join(contextSpec.ArtifactDir, buildArtifactsManifest)
			if err := writeJSONPB(artifacts, path); err != nil {
				if affectedErr != nil {
					return fmt.Errorf("%s (original error: %w)", err, affectedErr)
				}
				return err
			}
		}
		return affectedErr
	})
}
