// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"os"

	"github.com/google/subcommands"
)

const (
	fsshCmdName = "fssh"
)

func insertFSSHToCmd(args []string, position int) []string {
	if len(args) == position {
		return append(args, fsshCmdName)
	}
	args = append(args[:position+1], args[position:]...)
	args[position] = fsshCmdName
	return args
}

func main() {
	// Hack to support a default subcommand.
	if len(os.Args) == 1 || (os.Args[1] != "tunnel" && os.Args[1] != "sync-keys" && os.Args[1] != "fssh") {
		os.Args = insertFSSHToCmd(os.Args, 1)
	}

	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(&fsshCmd{}, "")
	subcommands.Register(&tunnelCmd{}, "")
	subcommands.Register(&syncKeysCmd{}, "")

	flag.Parse()
	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
