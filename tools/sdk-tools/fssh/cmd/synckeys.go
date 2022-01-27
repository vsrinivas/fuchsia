// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"

	"github.com/google/subcommands"
)

type syncKeysCmd struct {
}

func (*syncKeysCmd) Name() string { return "sync-keys" }

func (*syncKeysCmd) Synopsis() string {
	return "Sync SSH key files associated with Fuchsia between a local and remote workstation."
}

func (*syncKeysCmd) Usage() string {
	return "fssh sync-keys\n"
}

func (c *syncKeysCmd) SetFlags(f *flag.FlagSet) {
}

func (c *syncKeysCmd) Execute(_ context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	return subcommands.ExitSuccess
}
