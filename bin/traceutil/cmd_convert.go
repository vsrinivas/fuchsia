package main

import (
	"context"
	"flag"

	"github.com/google/subcommands"
)

type cmdConvert struct {
}

func NewCmdConvert() *cmdConvert {
	cmd := &cmdConvert{}
	return cmd
}

func (*cmdConvert) Name() string {
	return "convert"
}

func (*cmdConvert) Synopsis() string {
	return "Convert a JSON trace to a viewable HTML trace."
}

func (cmd *cmdConvert) Usage() string {
	usage := "traceutil convert FILE ...\n"
	return usage
}

func (cmd *cmdConvert) SetFlags(f *flag.FlagSet) {
}

func (cmd *cmdConvert) Execute(_ context.Context, f *flag.FlagSet,
	_ ...interface{}) subcommands.ExitStatus {
	if len(f.Args()) == 0 {
		return subcommands.ExitUsageError
	}

	ret := subcommands.ExitSuccess

	for _, jsonFilename := range f.Args() {
		htmlFilename := replaceFilenameExt(jsonFilename, "html")
		err := convertTrace(jsonFilename, htmlFilename)
		if err != nil {
			ret = subcommands.ExitFailure
		}
	}

	return ret
}
