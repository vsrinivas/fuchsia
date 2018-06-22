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
	buildcmd "fuchsia.googlesource.com/pm/cmd/pm/build"
	"fuchsia.googlesource.com/pm/cmd/pm/expand"
	"fuchsia.googlesource.com/pm/cmd/pm/genkey"
	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/cmd/pm/install"
	"fuchsia.googlesource.com/pm/cmd/pm/publish"
	"fuchsia.googlesource.com/pm/cmd/pm/seal"
	"fuchsia.googlesource.com/pm/cmd/pm/serve"
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
    archive - construct a single .far representation of the package
    expand  - expand a single .far representation of a package into a repository
    publish - publish the package to a local TUF directory
    serve   - serve a TUF directory of packages

Dev Only:
    install - install a single .far representation of the package
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
		err = archive.Run(cfg, flag.Args()[1:])

	case "build":
		err = buildcmd.Run(cfg, flag.Args()[1:])

	case "expand":
		err = expand.Run(cfg, flag.Args()[1:])

	case "genkey":
		err = genkey.Run(cfg, flag.Args()[1:])

	case "init":
		err = initcmd.Run(cfg, flag.Args()[1:])

	case "install":
		err = install.Run(cfg, flag.Args()[1:])

	case "publish":
		err = publish.Run(cfg, flag.Args()[1:])

	case "seal":
		err = seal.Run(cfg, flag.Args()[1:])

	case "sign":
		err = sign.Run(cfg, flag.Args()[1:])

	case "serve":
		err = serve.Run(cfg, flag.Args()[1:])

	case "update":
		err = update.Run(cfg, flag.Args()[1:])

	case "verify":
		err = verify.Run(cfg, flag.Args()[1:])

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
