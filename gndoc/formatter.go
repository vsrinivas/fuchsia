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

const (
	pageTitle = "GN Build Arguments"
	nameDepth = 3
)

// arg holds the name, comment description, default values, and any current overriden values.
type arg struct {
	name        string
	comment     string
	defaultVals []ArgValue
	currentVals []ArgValue
	keys        []string
}

// NewArg creates a new arg from a GNArg.
func newArg(gnarg GNArg, numKeys int) *arg {
	return &arg{
		name:    gnarg.Name,
		comment: gnarg.Comment,
		keys:    make([]string, 0, numKeys),
	}
}

// write emits the name, comment description, and value(s) of the argument in Markdown.
func (a *arg) write(out io.Writer, sources *SourceMap) {
	fmt.Fprintf(out, "\n%s %s\n", strings.Repeat("#", nameDepth), a.name)
	a.writeLinkifiedComment(out, sources)
	a.writeAllValues(out, sources)
}

func (a *arg) writeLinkifiedComment(out io.Writer, sources *SourceMap) {
	// TODO (juliehockett): Linkify //source/path in comment.
	fmt.Fprintf(out, "%s\n", a.comment)
}

// addKey adds a given key to the list of keys.
func (a *arg) addKey(key string) {
	if !contains(a.keys, key) {
		a.keys = append(a.keys, key)
	}
}

// addVal adds the given value to the list of values.
func (a *arg) addVal(val ArgValue, vals *[]ArgValue, key string) {
	if val.isPresent() {
		if idx, exists := hasVal(val, *vals); !exists {
			val.keys = append(val.keys, key)
			*vals = append(*vals, val)
		} else {
			(*vals)[idx].keys = append((*vals)[idx].keys, key)
		}
	}
}

func (a *arg) writeAllValues(out io.Writer, sources *SourceMap) {
	a.writeValues("Default", a.defaultVals, out, sources)
	a.writeValues("Current", a.currentVals, out, sources)
}

// writeValues emits all values in a list of ArgValues.
func (a *arg) writeValues(valType string, vals []ArgValue, out io.Writer, sources *SourceMap) {
	// If there is only one value, and that value present in for all keys, omit
	// the key name in the description.
	if len(vals) == 1 && len(vals[0].keys) == cap(a.keys) {
		fmt.Fprintf(out, "**%s value:** ", valType)
		vals[0].write(out, sources)
		return
	}

	// Otherwise, write the value and the key name(s) associated with it.
	for _, val := range vals {
		fmt.Fprintf(out, "**%s value for `%s`:** ", valType, strings.Join(val.keys, ", "))
		val.write(out, sources)
	}
}

// hasVal checks if a value is already in a given list, returning the existing one if present.
func hasVal(val ArgValue, args []ArgValue) (int, bool) {
	for idx, arg := range args {
		if val.Val == arg.Val && val.File == arg.File && val.Line == arg.Line {
			return idx, true
		}
	}
	return 0, false
}

// ArgValue holds a value, its filepath and line number, and the build associated with the value.
type ArgValue struct {
	Val  interface{} `json:"value"`
	File string      `json:"file"`
	Line int         `json:"line"`
	keys []string    `json:"-"`
}

// isPresent returns true if the value has anything other than the default values in its fields.
func (a *ArgValue) isPresent() bool {
	return a.Val != nil || a.File != "" || a.Line != 0
}

// write emits the value of a given argument value, along with the associated Markdown link to its declaration and build (if present).
func (a *ArgValue) write(out io.Writer, sources *SourceMap) {
	if a.File == "" {
		// If there is no declaration file, emit just the value.
		fmt.Fprintf(out, "%v\n\n", a.Val)
	} else {
		// Otherwise, emit the value with a link to the declaration.
		link := sources.GetSourceLink(a.File, a.Line)
		if link == "" {
			fmt.Fprintf(out, "%v\n\n", a.Val)
			return
		}
		fmt.Fprintf(out, "[%v](%s)\n\n", a.Val, link)
	}
}

type ArgMap struct {
	allKeys []string
	args    map[string]*arg
	sources *SourceMap
}

// NewArgMap returns a pointer to an
func NewArgMap(numKeys int, sources *SourceMap) *ArgMap {
	return &ArgMap{
		allKeys: make([]string, 0, numKeys),
		args:    make(map[string]*arg),
		sources: sources,
	}
}

func (a *ArgMap) AddArgs(gnArgs <-chan GNArg, keys <-chan string) {
	for arg := range gnArgs {
		a.addArg(arg, cap(keys))
	}
	for key := range keys {
		if !contains(a.allKeys, key) {
			a.allKeys = append(a.allKeys, key)
		}
	}
}

// addArg creates an arg from a GNArg and adds it to the map
func (a *ArgMap) addArg(gnArg GNArg, numKeys int) {
	arg, ok := a.args[gnArg.Name]
	if !ok {
		arg = newArg(gnArg, numKeys)
		a.args[gnArg.Name] = arg
	}
	arg.addKey(gnArg.Key)
	arg.addVal(gnArg.DefaultVal, &arg.defaultVals, gnArg.Key)
	arg.addVal(gnArg.CurrentVal, &arg.currentVals, gnArg.Key)
}

// EmitMarkdown emits Markdown text for the map of arguments.
func (a *ArgMap) EmitMarkdown(out io.Writer) {
	// Emit a header.
	fmt.Fprintf(out, "# %s\n", pageTitle)
	for _, name := range a.sortedArgs() {
		arg := a.args[name]
		arg.write(out, a.sources)
		a.writeMissingKeys(arg.keys, out)
	}
}

func (a *ArgMap) writeMissingKeys(presentKeys []string, out io.Writer) {
	totalMissing := len(a.allKeys) - len(presentKeys)
	if totalMissing <= 0 {
		return
	}
	missingKeys := make([]string, 0, totalMissing)
	for _, key := range a.allKeys {
		if !contains(presentKeys, key) {
			missingKeys = append(missingKeys, key)
		}
	}
	fmt.Fprintf(out, "No values for `%s`.\n", strings.Join(missingKeys, "`, `"))
}

// sortedArgs returns the list of map keys in alphabetical order for iterating over.
func (a *ArgMap) sortedArgs() []string {
	keys := make([]string, 0, len(a.args))
	for k := range a.args {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func contains(keys []string, key string) bool {
	for _, k := range keys {
		if k == key {
			return true
		}
	}
	return false
}
