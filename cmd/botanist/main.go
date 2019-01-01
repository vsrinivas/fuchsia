// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"os"

	"github.com/google/subcommands"
)

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(&ZedbootCommand{}, "")
	subcommands.Register(&QEMUCommand{}, "")
	subcommands.Register(&RunCommand{}, "")

	flag.Parse()
	os.Exit(int(subcommands.Execute(context.Background())))
}
