// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"

	"fuchsia.googlesource.com/fuchsia/tools/fuzz/lib"
)

func main() {
	// Parse any global flags (e.g. those for glog)
	flag.Parse()

	// Force logging config, to help with debugging
	flag.Lookup("logtostderr").Value.Set("true")

	cmd, err := lib.ParseArgs(flag.Args())
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error parsing args: %s\n", err)
		os.Exit(1)
	}

	if err := cmd.Execute(os.Stdout); err != nil {
		fmt.Fprintf(os.Stderr, "Error executing command: %s\n", err)
		os.Exit(1)
	}
}
