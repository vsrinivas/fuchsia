// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"

	"go.fuchsia.dev/fuchsia/sdk/cts/tools/testdriver"
)

var (
	sdkVersion = flag.String("sdk_version", "", "SDK version to execute the CTS against.")
	workspace  = flag.String("workspace", "", "Location to store downloaded artifacts.")
	manifest   = flag.String("manifest", "", "Location of the manifest defining CTS tests and dependencies.")
	logLevel   = flag.Int("log_level", 0, "Log message verbosity. 0 == minimal logs, with higher numbers adding more.")
)

func mainImpl() error {
	flag.Parse()

	d := testdriver.NewDriver()

	if *sdkVersion == "" {
		return fmt.Errorf("'sdk_version' must be provided.")
	}
	d.SetSDKVersion(*sdkVersion)

	if *workspace == "" {
		return fmt.Errorf("'workspace' must be provided.")
	}
	d.SetWorkspacePath(*workspace)

	if *manifest == "" {
		return fmt.Errorf("'manifest' must be provided.")
	}
	d.SetManifestPath(*manifest)

	if *logLevel == 0 {
		log.SetOutput(ioutil.Discard)
	}

	return d.Run()
}

func main() {
	if err := mainImpl(); err != nil {
		fmt.Println(err)
		os.Exit(1)
	}
}
