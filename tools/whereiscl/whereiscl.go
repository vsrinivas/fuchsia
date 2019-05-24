// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// whereiscl is a command-line utility that answers "Where is my CL?".
// Given a Gerrit review URL, it will answer
//   - Whether the CL was merged (or abandoned)
//   - Whether the CL passed Global Integration (if merged)
package main

import (
	"flag"
	"fmt"
	"log"
	"os"

	"fuchsia.googlesource.com/fuchsia/tools/whereiscl/lib"
)

func init() {
	flag.Usage = func() {
		fmt.Fprintf(flag.CommandLine.Output(), `Usage: whereiscl <review URL>

Answers whether a CL is merged (or abandoned) and whether it passed Global Integration.
Review URL can be various review URL forms as below. It can also be a raw CL number or a Change-Id.

Examples:
  $ whereiscl https://fuchsia-review.googlesource.com/c/fuchsia/+/123456789
  $ whereiscl fuchsia-review.googlesource.com/c/fuchsia/+/123456789/some/file
  $ whereiscl https://fuchsia-review.googlesource.com/c/fuchsia/+/Ie8dddbce1eeb01a561f3b36e1685f4136fb61378
  $ whereiscl http://fxr/123456789
  $ whereiscl http://fxr/Ie8dddbce1eeb01a561f3b36e1685f4136fb61378
  $ whereiscl fxr/123456789/some/file
  $ whereiscl 123456789
  $ whereiscl Ie8dddbce1eeb01a561f3b36e1685f4136fb61378

`)
		flag.PrintDefaults()
	}
}

func main() {
	flag.Parse()

	if flag.NArg() == 0 {
		// TODO: Consider alternatives. E.g., show all outstanding CLs
		// of the current user, or show all CLs that are pending in
		// Global Integration.
		flag.Usage()
		os.Exit(1)
	}

	queryInfo, err := lib.ParseReviewURL(flag.Arg(0))
	if err != nil {
		log.Fatalf("Error parsing the review URL: %v", err)
	}

	ci, err := lib.GetChangeInfo(queryInfo)
	if err != nil {
		log.Fatalf("Error getting change info: %v", err)
	}
	fmt.Printf("CL status: %v\n", ci.Status)

	if ci.Status != lib.CLStatusMerged {
		return
	}

	gs, err := lib.GetGIStatus(ci)
	if err != nil {
		log.Fatalf("Error getting GI status: %v", err)
	}
	fmt.Printf("GI status: %v\n", gs)
}
