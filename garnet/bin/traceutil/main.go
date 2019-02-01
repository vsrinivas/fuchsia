package main

import (
	"context"
	"flag"
	"os"

	"github.com/google/subcommands"
)

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")

	// TODO(TO-398): Add 'run' command.
	// TODO(TO-399): Add 'benchmark' command.
	subcommands.Register(NewCmdRecord(), "")
	subcommands.Register(NewCmdConvert(), "")

	flag.Parse()
	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
