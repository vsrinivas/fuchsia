// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bufio"
	"fmt"
	"os"
	"path"

	"github.com/golang/glog"
)

const baseTargetListFile = "base_packages.list"
const traceManagerPackageName = "trace_manager"

// fxbug.dev/23004: If tracing isn't in the base configuration then a lot of services
// will not have connected to trace-manager, resulting in potentially
// confusing traces. Until a better solution is found, at least give the user
// a heads-up to prevent wasting time wondering what the problem is.
//
// TODO(dje): It would make sense to only print this once a day or some such.

func checkBuildConfiguration() {
	if !traceManagerIsInBaseBuildTargets() {
		fmt.Printf("WARNING: %s is not in the base package set.\n", traceManagerPackageName)
		fmt.Printf("    Tracing will likely have missing data. fxbug.dev/23004\n")
		fmt.Printf("    To fix this, add --with-base=//garnet/packages/prod:tracing to \"fx set\"\n")
	}
}

func traceManagerIsInBaseBuildTargets() bool {
	file, err := os.Open(getBaseTargetListFile())
	if err != nil {
		glog.V(1).Infof("%s not found\n", baseTargetListFile)
		return false
	}

	scanner := bufio.NewScanner(file)

	for scanner.Scan() {
		package_name := scanner.Text()
		if package_name == traceManagerPackageName {
			return true
		}
	}

	return false
}

func getBaseTargetListFile() string {
	return path.Join(buildRoot, baseTargetListFile)
}
