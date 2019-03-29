// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"strings"

	fidlir "fidl/compiler/backend/types"
	gidlgolang "gidl/golang"
	gidlir "gidl/ir"
	gidlparser "gidl/parser"
)

// GIDLFlags for GIDL backends.
//
// --json <path> path to the JSON IR
// --gidl <path> path to the GIDL file
//
// TODO: specifcy backend to generate tests, e.g. go, rust, c++, etc.
type GIDLFlags struct {
	JSONPath *string
	GIDLPath *string
}

// Valid indicates whether the parsed Flags are valid to be used.
func (flags GIDLFlags) Valid() bool {
	return len(*flags.JSONPath) != 0 && len(*flags.GIDLPath) != 0
}

var flags = func() GIDLFlags {
	return GIDLFlags{
		JSONPath: flag.String("json", "",
			"relative path to the FIDL intermediate representation."),
		GIDLPath: flag.String("gidl", "",
			"relative path to the GIDL source."),
	}
}()

func parseGidlIr(filename string) gidlir.All {
	f, err := os.Open(filename)
	if err != nil {
		panic(err)
	}
	result, err := gidlparser.NewParser(filename, f).Parse()
	if err != nil {
		panic(err)
	}
	return result
}

func parseFidlJSONIr(filename string) fidlir.Root {
	bytes, err := ioutil.ReadFile(filename)
	if err != nil {
		panic(err)
	}
	var result fidlir.Root
	if err := json.Unmarshal(bytes, &result); err != nil {
		panic(err)
	}
	return result
}

func main() {
	flag.Parse()

	if !flag.Parsed() || !flags.Valid() {
		flag.PrintDefaults()
		os.Exit(1)
	}

	// Parse GIDL and JSON IR.
	var (
		gidl = parseGidlIr(*flags.GIDLPath)
		fidl = parseFidlJSONIr(*flags.JSONPath)
	)

	// For simplicity, we do not allow FIDL that GIDL depends on to have
	// dependent libraries. This makes it much simpler to have everything
	// in the IR, and avoid cross-references.
	if len(fidl.Libraries) != 0 {
		var libs []string
		for _, l := range fidl.Libraries {
			libs = append(libs, string(l.Name))
		}
		panic(fmt.Sprintf(
			"GIDL does not work with FIDL libraries with dependents, found: %s",
			strings.Join(libs, ",")))
	}

	// Generate Go code.
	buf := new(bytes.Buffer)
	err := gidlgolang.Generate(buf, gidl, fidl)
	if err != nil {
		panic(err)
	}
	_, err = os.Stdout.Write(buf.Bytes())
	if err != nil {
		panic(err)
	}
}
