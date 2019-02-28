// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ota

import (
	"bytes"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

var (
	fuchsiaDir      = flag.String("fuchsia-dir", os.Getenv("FUCHSIA_DIR"), "fuchsia dir")
	fuchsiaBuildDir = flag.String("fuchsia-build-dir", os.Getenv("FUCHSIA_BUILD_DIR"), "fuchsia build dir")
	sshKeyFile      = flag.String("ssh-private-key", "", "SSH private key file that can access the device")
	repoDir         = flag.String("repo-dir", "", "amber repository dir")
	zirconToolsDir  = flag.String("zircon-tools-dir", os.Getenv("ZIRCON_TOOLS_DIR"), "zircon tools dir")
	localHostname   = flag.String("local-hostname", "", "local hostname")
	deviceName      = flag.String("device", "", "device name")
	deviceHostname  = flag.String("device-hostname", "", "device hostname")
	localDevmgrPath = flag.String("devmgr-config", "", "path to the new system version devmgr config file")

	localDevmgrConfig []byte
)

func needFuchsiaDir() {
	if *fuchsiaDir == "" {
		log.Fatalf("either pass -fuchsia-dir or set $FUCHSIA_DIR")
	}
}

func needFuchsiaBuildDir() {
	if *fuchsiaBuildDir == "" {
		log.Fatalf("either pass -fuchsia-build-dir or set $FUCHSIA_BUILD_DIR")
	}
}

func needZirconToolsDir() {
	if *zirconToolsDir == "" {
		log.Fatalf("either pass -zircon-tools-dir or set $ZIRCON_TOOLS_DIR")
	}
}

func init() {
	var err error

	flag.Parse()

	if *deviceName != "" && *deviceHostname != "" {
		log.Fatalf("-device and -device-hostname are incompatible")
	}

	if *sshKeyFile == "" {
		needFuchsiaDir()
		*sshKeyFile = filepath.Join(*fuchsiaDir, ".ssh", "pkey")
	}

	if *repoDir == "" {
		needFuchsiaBuildDir()
		*repoDir = filepath.Join(*fuchsiaBuildDir, "amber-files", "repository")
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

	if *localDevmgrPath == "" {
		needFuchsiaBuildDir()
		*localDevmgrPath = filepath.Join(*fuchsiaBuildDir, "obj", "build", "images", "devmgr_config.txt")
	}

	// We want to make sure that /boot/config/devmgr config file changed to the
	// value we expect. First, read the file from the build.
	localDevmgrConfig, err = ioutil.ReadFile(*localDevmgrPath)
	if err != nil {
		log.Fatalf("failed to read %q: %s", localDevmgrPath, err)
	}
}

func runOutput(name string, arg ...string) ([]byte, []byte, error) {
	log.Printf("running: %s %q", name, arg)
	c := exec.Command(name, arg...)
	var o bytes.Buffer
	var e bytes.Buffer
	c.Stdout = &o
	c.Stderr = &e
	err := c.Run()
	stdout := o.Bytes()
	stderr := e.Bytes()
	log.Printf("stdout: %s", stdout)
	log.Printf("stderr: %s", stderr)
	return stdout, stderr, err
}

func netaddr(arg ...string) (string, error) {
	stdout, stderr, err := runOutput(filepath.Join(*zirconToolsDir, "netaddr"), arg...)
	if err != nil {
		return "", fmt.Errorf("netaddr failed: %s: %s", err, string(stderr))
	}
	return strings.TrimRight(string(stdout), "\n"), nil
}
