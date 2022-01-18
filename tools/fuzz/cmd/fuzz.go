// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/golang/glog"
	"go.fuchsia.dev/fuchsia/tools/fuzz"
)

func exitWithError(message string, err error) {
	glog.Errorf(message, err)
	// Also print to stdout, as defined by clusterfuchsia API
	fmt.Fprintf(os.Stdout, message, err)
	os.Exit(1)
}

func main() {
	// Parse any global flags (e.g. those for glog)
	flag.Parse()

	// Force logging config, to help with debugging
	flag.Lookup("logtostderr").Value.Set("true")

	cmd, err := fuzz.ParseArgs(flag.Args())
	if err != nil {
		exitWithError("Error parsing args: %s\n", err)
	}

	if err := cmd.Execute(os.Stdout); err != nil {
		exitWithError("Error executing command: %s\n", err)
	}
}
