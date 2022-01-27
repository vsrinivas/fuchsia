// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"

	"github.com/google/subcommands"
)

type tunnelCmd struct {
}

func (*tunnelCmd) Name() string { return "tunnel" }

func (*tunnelCmd) Synopsis() string {
	return "Create a tunnel between a local Fuchsia device and a remote host."
}

func (*tunnelCmd) Usage() string {
	return "fssh tunnel\n"
}

func (c *tunnelCmd) SetFlags(f *flag.FlagSet) {
}

func (c *tunnelCmd) Execute(_ context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	return subcommands.ExitSuccess
}
