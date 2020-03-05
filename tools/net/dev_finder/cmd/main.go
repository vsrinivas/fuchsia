// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

// Uses mDNS for Fuchsia device discovery.

import (
	"context"
	"flag"
	"log"
	"os"

	"github.com/google/subcommands"
)

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(&listCmd{}, "")
	subcommands.Register(&resolveCmd{}, "")

	log.SetFlags(log.Lshortfile)

	flag.Parse()
	os.Exit(int(subcommands.Execute(context.Background())))
}
