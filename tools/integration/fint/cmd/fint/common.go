// Copyright 2021 The Fuchsia Authors. All rights reserved.
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

	"github.com/google/subcommands"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"

	"go.fuchsia.dev/fuchsia/tools/integration/fint"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	fuchsiaDirEnvVar = "FUCHSIA_DIR"
)

// BaseCommand contains the logic shared by all fint subcommands: common flags,
// shared spec-loading logic, etc.
type BaseCommand struct {
	staticSpecPath  string
	contextSpecPath string
}

func (c *BaseCommand) SetFlags(f *flag.FlagSet) {
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

// execute contains the common logic shared by all fint subcommands. It
// validates the input flags and then runs a function corresponding to a
// subcommand. Suitable for calling directly from the `Execute` method of a
// subcommand.
func (c *BaseCommand) execute(ctx context.Context, f func(context.Context) error) subcommands.ExitStatus {
	if c.staticSpecPath == "" {
		logger.Errorf(ctx, "-static flag is required")
		return subcommands.ExitUsageError
	}
	if err := f(ctx); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

// loadSpecs loads the fint static and context specs from files specified by the
// input flags. Subcommands should call this function to obtain their input
// specs.
func (c *BaseCommand) loadSpecs() (*fintpb.Static, *fintpb.Context, error) {
	staticSpec, err := fint.ReadStatic(c.staticSpecPath)
	if err != nil {
		return nil, nil, err
	}

	var contextSpec *fintpb.Context
	if c.contextSpecPath != "" {
		contextSpec, err = fint.ReadContext(c.contextSpecPath)
		if err != nil {
			return nil, nil, err
		}
	} else {
		// The -context flag should always be set in production, but fall back
		// to looking up the `fuchsiaDirEnvVar` to determine the checkout and
		// build directories to make fint less cumbersome to run manually.
		contextSpec, err = defaultContextSpec()
		if err != nil {
			return nil, nil, err
		}
	}

	return staticSpec, contextSpec, nil
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

func writeJSONPB(pb proto.Message, path string) error {
	b, err := protojson.MarshalOptions{UseProtoNames: true}.Marshal(pb)
	if err != nil {
		return err
	}
	return ioutil.WriteFile(path, b, 0o644)
}
