// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"github.com/golang/protobuf/jsonpb"
	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/tools/integration/fint"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

const (
	fuchsiaDirEnvVar = "FUCHSIA_DIR"

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
	staticSpecPath  string
	contextSpecPath string
}

func (*SetCommand) Name() string { return "set" }

func (*SetCommand) Synopsis() string { return "runs gn gen with args based on the input specs." }

func (*SetCommand) Usage() string {
	return `fint set -static <path> [-context <path>]

flags:
`
}

func (c *SetCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&c.staticSpecPath, "static", "", "path to a Static .textproto file.")
	f.StringVar(
		&c.contextSpecPath,
		"context",
		"",
		("path to a Context .textproto file. If unset, the " +
			fuchsiaDirEnvVar +
			" will be used to locate the checkout."),
	)
}

func (c *SetCommand) Execute(ctx context.Context, _ *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if c.staticSpecPath == "" {
		logger.Errorf(ctx, "-static flag is required")
		return subcommands.ExitUsageError
	}
	if err := c.run(ctx); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (c *SetCommand) run(ctx context.Context) error {
	bytes, err := ioutil.ReadFile(c.staticSpecPath)
	if err != nil {
		return err
	}

	staticSpec, err := parseStatic(string(bytes))
	if err != nil {
		return err
	}

	var contextSpec *fintpb.Context
	if c.contextSpecPath != "" {
		bytes, err = ioutil.ReadFile(c.contextSpecPath)
		if err != nil {
			return err
		}

		contextSpec, err = parseContext(string(bytes))
		if err != nil {
			return err
		}
	} else {
		// The -context flag should always be set in production, but fall back
		// to looking up the `fuchsiaDirEnvVar` to determine the checkout and
		// build directories to make fint less cumbersome to run manually.
		contextSpec, err = defaultContextSpec()
		if err != nil {
			return err
		}
	}

	artifacts, setErr := fint.Set(ctx, staticSpec, contextSpec)

	if contextSpec.ArtifactDir != "" {
		f, err := osmisc.CreateFile(filepath.Join(contextSpec.ArtifactDir, artifactsManifest))
		if err != nil {
			return fmt.Errorf("failed to create artifacts file: %w", err)
		}
		defer f.Close()
		m := jsonpb.Marshaler{}
		if err := m.Marshal(f, artifacts); err != nil {
			return fmt.Errorf("failed to write artifacts file: %w", err)
		}
	}

	return setErr
}

func defaultContextSpec() (*fintpb.Context, error) {
	checkoutDir, found := os.LookupEnv(fuchsiaDirEnvVar)
	if !found {
		return nil, fmt.Errorf("$%s must be set if -context is not set", fuchsiaDirEnvVar)
	}
	return &fintpb.Context{
		CheckoutDir: checkoutDir,
		BuildDir:    filepath.Join(checkoutDir, "out", "default"),
	}, nil
}
