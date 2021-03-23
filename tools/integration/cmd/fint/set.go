// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"google.golang.org/protobuf/encoding/protojson"
)

const (
	// artifactsManifest is the name of the file (in `contextSpec.ArtifactsDir`)
	// that will expose manifest files and other metadata produced by this
	// command to the caller. We use JSON instead of textproto for this message
	// because it passes data from fint back to the caller, but the source of
	// truth for the proto definition is in fint. Proto libraries don't have
	// good support for deserializing unknown fields in textprotos (unlike JSON
	// protos), so if we used textproto then we'd have to make two separate fint
	// changes every time we want to start setting a new field: one to add the
	// field, then another to set the field, which can only be landed after the
	// updated proto definition has been propagated to all consumers.
	artifactsManifest = "set_artifacts.json"
)

type SetCommand struct {
	BaseCommand
}

func (*SetCommand) Name() string { return "set" }

func (*SetCommand) Synopsis() string { return "runs gn gen with args based on the input specs." }

func (*SetCommand) Usage() string {
	return `fint set -static <path> [-context <path>]

flags:
`
}

func (c *SetCommand) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	return c.execute(ctx, func(ctx context.Context) error {
		staticSpec, contextSpec, err := c.loadSpecs()
		if err != nil {
			return err
		}

		artifacts, setErr := fint.Set(ctx, staticSpec, contextSpec)

		if contextSpec.ArtifactDir != "" {
			b, err := protojson.Marshal(artifacts)
			if err != nil {
				return fmt.Errorf("failed to marshal artifacts: %w", err)
			}
			f, err := osmisc.CreateFile(filepath.Join(contextSpec.ArtifactDir, artifactsManifest))
			if err != nil {
				return fmt.Errorf("failed to create artifacts file: %w", err)
			}
			defer f.Close()
			if _, err := f.Write(b); err != nil {
				return fmt.Errorf("failed to write artifacts file: %w", err)
			}
		}

		return setErr
	})
}
