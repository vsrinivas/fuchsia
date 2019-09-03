// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gndoc

import (
	"fmt"
	"io"
	"regexp"
	"sort"
	"strings"
)

const (
	pageTitle = "GN Build Arguments"
	nameDepth = 3
)

var (
	linkRegexp = regexp.MustCompile("//([/A-Za-z-_]+)([.][/A-Za-z-_]+)?")
)

// writeArg emits the name, comment description, and value(s) of the argument in Markdown.
func writeArgs(args []Arg, out io.Writer, sources *SourceMap) {
	if len(args) == 0 {
		return
	}
	sort.Slice(args, func(i, j int) bool {
		return args[i].Name < args[j].Name
	})

	fmt.Fprintf(out, "%s %s\n", strings.Repeat("#", nameDepth), args[0].Name)
	// TODO (juliehockett): Make sure that *all* comments get emitted.
	writeLinkifiedComment(&args[0], out, sources)
	writeAllValues(args, out, sources)
}

// writeValue emits the value of a given argument value, along with the associated Markdown link to its declaration and build (if present).
func writeValue(a *argValue, out io.Writer, sources *SourceMap) {
	var value string
	if strings.Contains(a.Val, "\n") {
		value = fmt.Sprintf("\n```\n%s\n```", a.Val)
	} else {
		value = fmt.Sprintf(" `%s`", a.Val)
	}

	if a.File == "" {
		// If there is no declaration file, emit just the value.
		fmt.Fprintf(out, "%s\n\n", value)
	} else {
		// Otherwise, emit the value with a link to the declaration.
		link := sources.GetSourceLink(a.File, a.Line)
		if link == "" {
			fmt.Fprintf(out, "%s\n\nFrom %s:%d\n\n", value, a.File, a.Line)
			return
		}
		fmt.Fprintf(out, "%s\n\nFrom [%s:%d](%s)\n\n", value, a.File, a.Line, link)
	}
}

func writeLinkifiedComment(a *Arg, out io.Writer, sources *SourceMap) {
	replFunc := func(str string) string {
		if link := sources.GetSourceLink(str, 0); link != "" {
			return fmt.Sprintf("[%s](%s)", str, link)
		}
		return str
	}
	fmt.Fprintf(out, "%s\n", linkRegexp.ReplaceAllStringFunc(a.Comment, replFunc))
}

func writeAllValues(args []Arg, out io.Writer, sources *SourceMap) {
	emptyArgValue := argValue{}
	for _, a := range args {
		if a.CurrentVal == emptyArgValue || a.CurrentVal == a.DefaultVal {
			fmt.Fprintf(out, "**Current value (from the default):**")
			writeValue(&a.DefaultVal, out, sources)
			return
		}
		fmt.Fprintf(out, "**Current value for `%s`:**", a.Key)
		writeValue(&a.CurrentVal, out, sources)
		fmt.Fprintf(out, "**Overridden from the default:**")
		writeValue(&a.DefaultVal, out, sources)
	}
}
