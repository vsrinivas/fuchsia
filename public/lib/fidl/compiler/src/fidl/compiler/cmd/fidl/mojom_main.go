// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"flag"
	"fmt"
	"log"
	"os"
)

// This file contains the main() for the mojom tool binary.
// The mojom tool is used to process .mojom files.
//
// The tool is invoked as follows:
//
//		mojom <command> [<arguments>]
//
//	where <commands> is one of:
//		- parse
//		- fmt
//
//	For further information about each command, see the file named
//	<command>_cmd.go for example "parse_cmd.go" and "fmt_cmd.go".
func main() {
	log.SetFlags(0)
	commands := NewCommandSet()
	commands.AddCommand("fmt", fmtCmd, "Formats a mojom file.")
	commands.AddCommand("gen", genCmd, "Generates bindings from mojom files.")
	commands.AddCommand("parse", parseCmd, "Parses mojom files.")
	commands.AddHelpCommand()
	commands.RunCommand(os.Args)
}

type command struct {
	Name string
	Func func([]string)
	Desc string
}

type commandSet struct {
	commandNames []string
	commandMap   map[string]command
}

func NewCommandSet() *commandSet {
	commandSet := new(commandSet)
	commandSet.commandMap = make(map[string]command)
	return commandSet
}

func (c *commandSet) AddCommand(name string, f func([]string), desc string) {
	if _, ok := c.commandMap[name]; ok {
		panic(fmt.Sprintf("Tried to add a second command with the name: %s", name))
	}
	c.commandNames = append(c.commandNames, name)
	c.commandMap[name] = command{name, f, desc}
}

func (c *commandSet) Usage(toolName string) string {
	b := bytes.Buffer{}
	b.WriteString(fmt.Sprintf("%s is a tool for managing .mojom files.\n\n", toolName))
	b.WriteString("Usage:\n\n")
	b.WriteString(fmt.Sprintf("\t%s <command> [<arguments>]\n\n", toolName))
	b.WriteString("The commands are:\n\n")

	for _, name := range c.commandNames {
		b.WriteString(fmt.Sprintf("\t%s\t%s\n", name, c.commandMap[name].Desc))
	}
	return b.String()
}

func (c *commandSet) AddHelpCommand() {
	helpCmd := func(args []string) {
		fmt.Print(c.Usage(args[0]))
	}
	c.AddCommand("help", helpCmd, "Prints out this help message.")
}

func (c *commandSet) RunCommand(args []string) {
	if len(args) < 2 {
		fmt.Println("No command specified.")
		fmt.Print(c.Usage(args[0]))
		os.Exit(1)
	}

	cmd, ok := c.commandMap[args[1]]
	if !ok {
		fmt.Printf("%s is not a recognized command.\n", args[1])
		fmt.Print(c.Usage(args[0]))
		os.Exit(1)
	}

	cmd.Func(args)
}

// UsageString constructs a usage string for the flags of a FlagSet.
func UsageString(f *flag.FlagSet) string {
	b := bytes.Buffer{}

	// isZeroValue guesses if the string represents the zero value for a flag.
	// isZeroValue is used to determine if the default value of a flag should be
	// printed in its usage string.
	isZeroValue := func(value string) bool {
		switch value {
		case "false":
			return true
		case "":
			return true
		case "0":
			return true
		}
		return false
	}

	// Iterate over the flags and for each of them generate its usage string
	// and add it to the flag set's usage string.
	f.VisitAll(func(f *flag.Flag) {
		s := fmt.Sprintf("  -%s", f.Name) // Two spaces before -; see next two comments.
		// Four spaces before the tab triggers good alignment
		// for both 4- and 8-space tab stops.
		s += "\n    \t"
		s += f.Usage
		if !isZeroValue(f.DefValue) {
			s += fmt.Sprintf(" (default %q)", f.DefValue)
		}
		b.WriteString(fmt.Sprint(s, "\n"))
	})

	return b.String()
}
