// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"flag"
	"fmt"
	"io"
	"os"

	"github.com/golang/glog"
)

// API version
const (
	VersionMajor = 0
	VersionMinor = 1
	VersionPatch = 0
)

// Available subcommand names
// TODO(fxbug.dev/47231): add resolve_fuzzer command
const (
	StartInstance = "start_instance"
	StopInstance  = "stop_instance"
	ListFuzzers   = "list_fuzzers"
	RunFuzzer     = "run_fuzzer"
	GetData       = "get_data"
	PutData       = "put_data"
	Version       = "version"
)

var commandDesc = map[string]string{
	StartInstance: "Start a Fuchsia instance",
	StopInstance:  "Stop a Fuchsia instance",
	ListFuzzers:   "List available fuzz targets on an instance",
	RunFuzzer:     "Run a fuzz target on an instance (passing any extra args to libFuzzer)",
	GetData:       "Copy files between an instance and a local path",
	PutData:       "Copy files between a local path and an instance",
	Version:       "Get API version",
}

// An APICommand is the structured result of parsing the command-line args
type APICommand struct {
	name string

	handle  string
	fuzzer  string
	srcPath string
	dstPath string

	extraArgs []string
}

// Execute the APICommand, writing any output to the given io.Writer
func (c *APICommand) Execute(out io.Writer) error {
	var instance Instance

	glog.Infof("Running API command: %v\n", c)

	// For commands that take a handle, load the Instance from the handle
	if c.handle != "" {
		handle, err := LoadHandleFromString(c.handle)
		if err != nil {
			return fmt.Errorf("Bad handle: %s", err)
		}

		if instance, err = loadInstanceFromHandle(handle); err != nil {
			return fmt.Errorf("Bad handle: %s", err)
		}
		defer instance.Close()
	}

	switch c.name {
	case StartInstance:
		instance, err := NewInstance()
		if err != nil {
			return fmt.Errorf("Error creating instance: %s", err)
		}

		glog.Info("Starting instance...")
		if err := instance.Start(); err != nil {
			return fmt.Errorf("Error starting instance: %s", err)
		}
		glog.Info("Instance started.")
		defer instance.Close()

		handle, err := instance.Handle()
		if err != nil {
			return fmt.Errorf("Error getting instance handle: %s", err)
		}
		fmt.Fprintf(out, "%s\n", handle.Serialize())
	case StopInstance:
		return instance.Stop()
	case ListFuzzers:
		for _, name := range instance.ListFuzzers() {
			fmt.Fprintf(out, "%s\n", name)
		}
	case GetData:
		return instance.Get(c.srcPath, c.dstPath)
	case PutData:
		return instance.Put(c.srcPath, c.dstPath)
	case RunFuzzer:
		// TODO(fxbug.dev/45431): buffer output so we don't get prematurely terminated by CF
		return instance.RunFuzzer(out, c.fuzzer, c.extraArgs...)
	case Version:
		fmt.Fprintf(out, "v%d.%d.%d\n", VersionMajor, VersionMinor, VersionPatch)
	}
	return nil
}

// ParseArgs converts command-line args into an API-command
func ParseArgs(args []string) (*APICommand, error) {
	if len(args) == 0 {
		printUsage()
		return nil, fmt.Errorf("missing subcommand")
	}

	cmd := &APICommand{name: args[0]}

	flagSet := flag.NewFlagSet(cmd.name, flag.ContinueOnError)
	handleDesc := fmt.Sprintf("an instance `handle`, as returned by %s", StartInstance)
	fuzzerDesc := fmt.Sprintf("a `fuzzer` name, as returned by %s", ListFuzzers)

	var requiredArgs []*string

	switch cmd.name {
	case StartInstance, Version:
	case StopInstance:
		flagSet.StringVar(&cmd.handle, "handle", "", handleDesc)
		requiredArgs = []*string{&cmd.handle}
	case ListFuzzers:
		flagSet.StringVar(&cmd.handle, "handle", "", handleDesc)
		requiredArgs = []*string{&cmd.handle}
	case RunFuzzer:
		flagSet.StringVar(&cmd.handle, "handle", "", handleDesc)
		flagSet.StringVar(&cmd.fuzzer, "fuzzer", "", fuzzerDesc)
		requiredArgs = []*string{&cmd.handle, &cmd.fuzzer}
	case GetData:
		flagSet.StringVar(&cmd.handle, "handle", "", handleDesc)
		flagSet.StringVar(&cmd.fuzzer, "fuzzer", "", fuzzerDesc)
		flagSet.StringVar(&cmd.srcPath, "src", "", "target source `path` (may include glob)")
		flagSet.StringVar(&cmd.dstPath, "dst", "", "host destination `path`")
		requiredArgs = []*string{&cmd.handle, &cmd.fuzzer, &cmd.srcPath, &cmd.dstPath}
	case PutData:
		flagSet.StringVar(&cmd.handle, "handle", "", handleDesc)
		flagSet.StringVar(&cmd.fuzzer, "fuzzer", "", fuzzerDesc)
		flagSet.StringVar(&cmd.srcPath, "src", "", "host source `path` (may include glob)")
		flagSet.StringVar(&cmd.dstPath, "dst", "", "target destination `path`")
		requiredArgs = []*string{&cmd.handle, &cmd.fuzzer, &cmd.srcPath, &cmd.dstPath}
	default:
		printUsage()
		return nil, fmt.Errorf("unknown subcommand: %s", cmd.name)
	}

	if err := flagSet.Parse(args[1:]); err != nil {
		// Usage will already have been printed by Parse() in this case
		return nil, err
	}

	// Handle missing arguments
	for _, arg := range requiredArgs {
		if *arg == "" {
			flagSet.Usage()
			return nil, fmt.Errorf("not enough arguments")
		}
	}

	// Handle any extra args
	if flagSet.NArg() > 0 {
		if cmd.name == RunFuzzer {
			cmd.extraArgs = flagSet.Args()
		} else {
			flagSet.Usage()
			return nil, fmt.Errorf("too many arguments")
		}
	}

	return cmd, nil
}

func printUsage() {
	fmt.Printf("Usage: %s <subcommand>\n\n", os.Args[0])
	fmt.Printf("Supported subcommands:\n")
	for name, desc := range commandDesc {
		fmt.Printf(" - %s: %s\n", name, desc)
	}
}
