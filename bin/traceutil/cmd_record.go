package main

import (
	"context"
	"flag"
	"fmt"
	"time"

	"github.com/google/subcommands"
)

type cmdRecord struct {
	flags *flag.FlagSet

	targetHostname string
	filePrefix     string
	captureConfig  *captureTraceConfig
}

func NewCmdRecord() *cmdRecord {
	cmd := &cmdRecord{
		flags: flag.NewFlagSet("record", flag.ExitOnError),
	}
	cmd.flags.StringVar(&cmd.filePrefix, "file-prefix", "",
		"Prefix for trace file names.  Defaults to 'trace-<timestamp>'.")
	cmd.flags.StringVar(&cmd.targetHostname, "target", "", "Target hostname.")

	cmd.captureConfig = newCaptureTraceConfig(cmd.flags)
	return cmd
}

func (*cmdRecord) Name() string {
	return "record"
}

func (*cmdRecord) Synopsis() string {
	return "Record a trace on a target, download, and convert to HTML."
}

func (cmd *cmdRecord) Usage() string {
	usage := "traceuitl record <options>\n"
	cmd.flags.Visit(func(flag *flag.Flag) {
		usage += flag.Usage
	})

	return usage
}

func (cmd *cmdRecord) SetFlags(f *flag.FlagSet) {
	*f = *cmd.flags
}

func (cmd *cmdRecord) Execute(_ context.Context, f *flag.FlagSet,
	_ ...interface{}) subcommands.ExitStatus {
	conn, err := NewTargetConnection(cmd.targetHostname)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}
	defer conn.Close()

	err = captureTrace(cmd.captureConfig, conn)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}

	prefix := cmd.getFilenamePrefix()
	jsonFilename := prefix + ".json"
	htmlFilename := prefix + ".html"

	fmt.Print("Downloading trace... ")
	// Should we use prefix on the remote file name as well?
	err = conn.GetFile("/data/trace.json", jsonFilename)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}
	fmt.Println("done")

	// TODO(TO-403): Remove remote file.  Add command line option to leave it.

	err = convertTrace(jsonFilename, htmlFilename)
	if err != nil {
		fmt.Println(err.Error())
		return subcommands.ExitFailure
	}

	return subcommands.ExitSuccess
}

func (cmd *cmdRecord) getFilenamePrefix() string {
	if cmd.filePrefix == "" {
		// Use ISO_8601 date time format.
		return "trace-" + time.Now().Format("2006-01-02T15:04:05")
	} else {
		return cmd.filePrefix
	}
}
