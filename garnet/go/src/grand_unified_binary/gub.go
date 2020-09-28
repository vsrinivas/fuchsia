// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Fuchsia Go Grand Unified Binary is a binary that contains several other
// binaries, so done in order to reduce the amount of disk space and shared
// memory required to run Fuchsia subsystems written in Go. This combination
// saves >=50% of storage and ram cost on a running system at boot/update time.
package main

import (
	"log"
	"os"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/amber/amberctl"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pkgfs/pkgsvr"
)

func main() {
	// The new fuchsia build templates do not support renaming a binary and properly
	// including its dependent libraries in a package. As a workaround, we leave the
	// binary name untouched and check the second argument if the first argument is
	// the default binary name.
	// TODO(fxbug.dev/55842): Remove this workaround.
	if len(os.Args) != 0 && filepath.Base(os.Args[0]) == "grand_unified_binary" {
		os.Args[0] = ""
		os.Args = os.Args[1:]
	}

	if len(os.Args) < 1 {
		log.Println("software delivery grand unified binary: cannot determine binary, no argv 0")
		os.Exit(1)
	}
	name := filepath.Base(os.Args[0])
	name = strings.SplitN(name, ".", 2)[0]
	switch name {
	case "pkgsvr":
		pkgsvr.Main()
	case "amberctl", "amber_ctl":
		amberctl.Main()
	case "netstack":
		netstack.Main()
	default:
		log.Printf("software delivery grand unified binary: unknown inner binary name: %s (%s)", name, os.Args[0])
		os.Exit(1)
	}
}
