// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/system_ota_test/util"
)

var (
	fuchsiaDir      = flag.String("fuchsia-dir", os.Getenv("FUCHSIA_DIR"), "fuchsia dir")
	sshKeyFile      = flag.String("ssh-private-key", "", "SSH private key file that can access the device")
	zirconToolsDir  = flag.String("zircon-tools-dir", os.Getenv("ZIRCON_TOOLS_DIR"), "zircon tools dir")
	localHostname   = flag.String("local-hostname", "", "local hostname")
	deviceName      = flag.String("device", "", "device name")
	deviceHostname  = flag.String("device-hostname", "", "device hostname")
	localDevmgrPath = flag.String("devmgr-config", "", "path to the new system version devmgr config file")
	lkgbPath        = flag.String("lkgb", "", "path to lkgb, default is $FUCHSIA_DIR/prebuilt/tools/lkgb/lkgb")
	artifactsPath   = flag.String("artifacts", "", "path to the artifacts binary, default is $FUCHSIA_DIR/prebuilt/tools/artifacts/artifacts")
	builderName     = flag.String("builder-name", "", "download the amber repository from the latest build of this builder")
	buildID         = flag.String("build-id", "", "download the amber repository from this build id")
)

func needFuchsiaDir() {
	if *fuchsiaDir == "" {
		log.Fatalf("either pass -fuchsia-dir or set $FUCHSIA_DIR")
	}
}

func needZirconToolsDir() {
	if *zirconToolsDir == "" {
		log.Fatalf("either pass -zircon-tools-dir or set $ZIRCON_TOOLS_DIR")
	}
}

const usage = `Usage: %s [options] <command>

System OTA Tests:
    upgrade - upgrade a device to the specified build
`

func doMain() int {
	var err error

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		flag.PrintDefaults()
	}
	flag.Parse()

	if *deviceName != "" && *deviceHostname != "" {
		log.Fatalf("-device and -device-hostname are incompatible")
	}

	if *sshKeyFile == "" {
		needFuchsiaDir()
		*sshKeyFile = filepath.Join(*fuchsiaDir, ".ssh", "pkey")
	}

	if *localHostname == "" {
		needZirconToolsDir()
		*localHostname, err = netaddr("--local", *deviceName)
		if err != nil {
			log.Fatalf("ERROR: netaddr failed: %s", err)
		}
		if *localHostname == "" {
			log.Fatalf("unable to determine the local hostname")
		}
	}

	if *deviceHostname == "" {
		needZirconToolsDir()
		*deviceHostname, err = netaddr("--nowait", "--timeout=1000", "--fuchsia", *deviceName)
		if err != nil {
			log.Fatalf("ERROR: netaddr failed: %s", err)
		}
		if *deviceHostname == "" {
			log.Fatalf("unable to determine the device hostname")
		}
	}

	if *lkgbPath == "" {
		needFuchsiaDir()
		*lkgbPath = filepath.Join(*fuchsiaDir, "prebuilt", "tools", "lkgb", "lkgb")
	}

	if *artifactsPath == "" {
		needFuchsiaDir()
		*artifactsPath = filepath.Join(*fuchsiaDir, "prebuilt", "tools", "artifacts", "artifacts")
	}

	switch flag.Arg(0) {
	case "upgrade":
		DoUpgradeTest(flag.Args()[1:])
	default:
		flag.Usage()
		return 1
	}

	return 0
}

func main() {
	os.Exit(doMain())
}

func netaddr(arg ...string) (string, error) {
	stdout, stderr, err := util.RunCommand(filepath.Join(*zirconToolsDir, "netaddr"), arg...)
	if err != nil {
		return "", fmt.Errorf("netaddr failed: %s: %s", err, string(stderr))
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}
