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

// AddArgs creates args from GNArgs and adds them to the maps
func (a *ArgMap) AddArgs(gnArgs <-chan Arg) {
	for gnArg := range gnArgs {
		a.AddArg(gnArg)
	}
}

// AddArg adds Arg to the maps
func (a *ArgMap) AddArg(gnArg Arg) {
	a.args[gnArg.Key] = append(a.args[gnArg.Key], gnArg)
	a.argLookup[gnArg.Name] = append(a.argLookup[gnArg.Name], gnArg.Key)
}

// EmitMarkdown emits Markdown text for the map of arguments.
func (a *ArgMap) EmitMarkdown(out io.Writer) {
	type mappedArgs struct {
		m map[string][]Arg
		k []string
	}
	sortedArgs := struct {
		m map[string]*mappedArgs
		k []string
	}{
		m: make(map[string]*mappedArgs),
		k: make([]string, 0),
	}

	numKeys := len(a.args)
	for _, gnArgs := range a.args {
		for _, gnArg := range gnArgs {
			// Lookup the keys associated with this arg & sort & stringify.
			keys, _ := a.argLookup[gnArg.Name]
			if len(keys) == numKeys {
				// Incoming keys will always have an `=`, and so  this is an
				// okay value.
				keys = []string{"all"}
			}
			sort.Strings(keys)
			key := strings.Join(keys, ", ")
			if _, ok := sortedArgs.m[key]; !ok {
				sortedArgs.m[key] = &mappedArgs{
					m: make(map[string][]Arg),
					k: make([]string, 0)}
			}
			sortedArgs.m[key].m[gnArg.Name] = append(sortedArgs.m[key].m[gnArg.Name], gnArg)
		}
	}
	for k := range sortedArgs.m {
		for argName := range sortedArgs.m[k].m {
			sort.Slice(sortedArgs.m[k].m[argName], func(i, j int) bool {
				return sortedArgs.m[k].m[argName][i].Key < sortedArgs.m[k].m[argName][j].Key
			})
			sortedArgs.m[k].k = append(sortedArgs.m[k].k, argName)
		}
		sort.Strings(sortedArgs.m[k].k)
		sortedArgs.k = append(sortedArgs.k, k)
	}
	sort.Strings(sortedArgs.k)
	// Emit a header.
	fmt.Fprintf(out, "# %s\n\n", pageTitle)
	for _, name := range sortedArgs.k {
		if name == "all" {
			fmt.Fprintf(out, "## All builds\n\n")
		} else {
			fmt.Fprintf(out, "## `%s`\n\n", name)
		}
		for _, argsKey := range sortedArgs.m[name].k {
			gnArgs := sortedArgs.m[name].m[argsKey]
			writeArgs(gnArgs, out, a.sources)
		}
	}
}
