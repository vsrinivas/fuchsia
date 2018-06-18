// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gndoc

import (
	"fmt"
	"io"
	"sort"
	"strings"
)

type ArgMap struct {
	argLookup map[string][]string // map of arg names to keys for lookup
	args      map[string][]Arg    // map of key names to relevant args
	sources   *SourceMap          // map of existing source locations for linkifying
}

// NewArgMap returns a pointer to an
func NewArgMap(sources *SourceMap) *ArgMap {
	return &ArgMap{
		argLookup: make(map[string][]string),
		args:      make(map[string][]Arg),
		sources:   sources,
	}
}

// AddArg creates args from GNArgs and adds them to the maps
func (a *ArgMap) AddArgs(gnArgs <-chan Arg) {
	for gnArg := range gnArgs {
		a.AddArg(gnArg)
	}
}

func (a *ArgMap) AddArg(gnArg Arg) {
	a.args[gnArg.Key] = append(a.args[gnArg.Key], gnArg)
	a.argLookup[gnArg.Name] = append(a.argLookup[gnArg.Name], gnArg.Key)
}

// sortedArgs returns the list of args in the appropriate order to print.
func (a *ArgMap) sortedArgs() (map[string]map[string][]Arg, []string) {
	numKeys := len(a.args)
	args := make(map[string]map[string][]Arg)
	// For each arg, we need to push it into the appropiate list.
	for _, gnArgs := range a.args {
		for _, gnArg := range gnArgs {
			// Lookup the keys associated with this arg & sort & stringify.
			keys, _ := a.argLookup[gnArg.Name]
			if len(keys) == numKeys {
				// Incoming keys will always have an `=`, and so this is an okay value.
				keys = []string{"all"}
			}
			key := strings.Join(keys, ", ")
			_, ok := args[key]
			if !ok {
				args[key] = make(map[string][]Arg)
			}
			args[key][gnArg.Name] = append(args[key][gnArg.Name], gnArg)
		}
	}

	// Get the keys in alphabetical order
	keys := make([]string, 0, numKeys)
	for k := range args {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	return args, keys
}

// EmitMarkdown emits Markdown text for the map of arguments.
func (a *ArgMap) EmitMarkdown(out io.Writer) {
	// Emit a header.
	fmt.Fprintf(out, "# %s\n\n", pageTitle)
	gnArgsMap, keys := a.sortedArgs()
	for _, name := range keys {
		if name == "all" {
			fmt.Fprintf(out, "## All builds\n\n")
		} else {
			fmt.Fprintf(out, "## `%s`\n\n", name)
		}

		for _, gnArgs := range gnArgsMap[name] {
			writeArgs(gnArgs, out, a.sources)
		}
	}
}
