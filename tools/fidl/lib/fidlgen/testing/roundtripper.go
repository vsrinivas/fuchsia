// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"flag"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Flag values, grouped into a struct to be kept out of the global namespace.
var flags struct {
	in  string
	out string
}

func init() {
	flag.StringVar(&flags.in, "in", "", "The input FIDL IR JSON file")
	flag.StringVar(&flags.out, "out", "", "The path at which to output the marshaled-unmarshaled FIDL IR JSON file")
}

func main() {
	flag.Parse()

	if flags.in == "" {
		log.Fatal("`-in` is a required argument")
	}
	if flags.out == "" {
		log.Fatal("`-out` is a required argument")
	}

	if err := execute(flags.in, flags.out); err != nil {
		log.Fatal(err)
	}
}

func execute(in, out string) error {
	ir, err := fidlgen.ReadJSONIr(in)
	if err != nil {
		return err
	}

	f, err := os.Create(out)
	if err != nil {
		return err
	}
	defer f.Close()

	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	return enc.Encode(ir)
}
