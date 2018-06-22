// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/cmd/pm/archive"
	"fuchsia.googlesource.com/pm/cmd/pm/expand"
	"fuchsia.googlesource.com/pm/cmd/pm/genkey"
	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/cmd/pm/install"
	"fuchsia.googlesource.com/pm/cmd/pm/seal"
	"fuchsia.googlesource.com/pm/cmd/pm/sign"
	"fuchsia.googlesource.com/pm/cmd/pm/update"
	"fuchsia.googlesource.com/pm/cmd/pm/verify"
)

const usage = `Usage: %s [-k key] [-m manifest] [-o output dir] [-t tempdir] <command>
Commands
    init    - initialize a package meta directory in the standard form
    genkey  - generate a new private key

    build   - perform update, sign and seal in order
      update  - update the merkle roots in meta/contents
      sign    - sign a package with the given key
      seal    - seal package metadata into a signed meta.far
		verify  - verify metadata signature against the embedded public key

	Dev Only:
    archive - construct a single .far representation of the package
    expand  - expand a single .far representation of a package into a repository
    install - install a single .far representation of the package

TODO:
    publish - upload the package to a distribution service
`

func main() {
	cfg := build.NewConfig()
	cfg.InitFlags(flag.CommandLine)

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		flag.PrintDefaults()
	}

	flag.Parse()

	var err error
	switch flag.Arg(0) {
	case "archive":
		err = archive.Run(cfg)

	case "build":
		err = update.Run(cfg)
		if err != nil {
			die(err)
		}
		err = sign.Run(cfg)
		if err != nil {
			die(err)
		}
		err = seal.Run(cfg)
		if err != nil {
			die(err)
		}

	case "expand":
		err = expand.Run(cfg)

	case "genkey":
		err = genkey.Run(cfg)

	case "init":
		err = initcmd.Run(cfg)

	case "install":
		err = install.Run(cfg)

	case "seal":
		err = seal.Run(cfg)

	case "sign":
		err = sign.Run(cfg)

	case "update":
		err = update.Run(cfg)

	case "verify":
		err = verify.Run(cfg)

	default:
		flag.Usage()
		os.Exit(1)
	}

	if err != nil {
		die(err)
	}
}

func die(err error) {
	fmt.Fprintf(os.Stderr, "%s\n", err)
	os.Exit(1)
}
