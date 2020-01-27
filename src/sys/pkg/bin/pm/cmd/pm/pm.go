// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime/trace"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/cmd/pm/archive"
	buildcmd "fuchsia.googlesource.com/pm/cmd/pm/build"
	"fuchsia.googlesource.com/pm/cmd/pm/delta"
	"fuchsia.googlesource.com/pm/cmd/pm/expand"
	"fuchsia.googlesource.com/pm/cmd/pm/genkey"
	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/cmd/pm/newrepo"
	"fuchsia.googlesource.com/pm/cmd/pm/publish"
	"fuchsia.googlesource.com/pm/cmd/pm/seal"
	"fuchsia.googlesource.com/pm/cmd/pm/serve"
	"fuchsia.googlesource.com/pm/cmd/pm/snapshot"
	"fuchsia.googlesource.com/pm/cmd/pm/update"
	"fuchsia.googlesource.com/pm/cmd/pm/verify"
)

const usage = `Usage: %s [-k key] [-m manifest] [-o output dir] [-t tempdir] <command> [-help]

Package Commands:
    init     - initialize a package meta directory in the standard form
    build    - perform update and seal in order
    update   - update the merkle roots in meta/contents
    seal     - seal package metadata into a meta.far
    verify   - verify metadata
    archive  - construct a single .far representation of the package

Repository Commands:
    newrepo  - create a new local repostory
    publish  - publish a package to a local repository
    serve    - serve a local repository
    expand   - (deprecated) expand an archive

Tools:
    snapshot - capture metadata from multiple packages in a single file
    delta    - compare two snapshot files

For help with individual commands run "pm <command> --help"
`

var tracePath = flag.String("trace", "", "write runtime trace to `file`")

func doMain() int {
	cfg := build.NewConfig()
	cfg.InitFlags(flag.CommandLine)

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		flag.PrintDefaults()
	}

	flag.Parse()

	if *tracePath != "" {
		tracef, err := os.Create(*tracePath)
		if err != nil {
			log.Fatal(err)
		}
		defer func() {
			if err := tracef.Sync(); err != nil {
				log.Fatal(err)
			}
			if err := tracef.Close(); err != nil {
				log.Fatal(err)
			}
		}()
		if err := trace.Start(tracef); err != nil {
			log.Fatal(err)
		}
		defer trace.Stop()
	}

	var err error
	switch flag.Arg(0) {
	case "archive":
		err = archive.Run(cfg, flag.Args()[1:])

	case "build":
		err = buildcmd.Run(cfg, flag.Args()[1:])

	case "delta":
		err = delta.Run(cfg, flag.Args()[1:])

	case "expand":
		err = expand.Run(cfg, flag.Args()[1:])

	case "genkey":
		err = genkey.Run(cfg, flag.Args()[1:])

	case "init":
		err = initcmd.Run(cfg, flag.Args()[1:])

	case "publish":
		err = publish.Run(cfg, flag.Args()[1:])

	case "seal":
		err = seal.Run(cfg, flag.Args()[1:])

	case "sign":
		fmt.Fprintf(os.Stderr, "sign is deprecated without replacement")
		err = nil

	case "serve":
		err = serve.Run(cfg, flag.Args()[1:], nil)

	case "snapshot":
		err = snapshot.Run(cfg, flag.Args()[1:])

	case "update":
		err = update.Run(cfg, flag.Args()[1:])

	case "verify":
		err = verify.Run(cfg, flag.Args()[1:])

	case "newrepo":
		err = newrepo.Run(cfg, flag.Args()[1:])

	default:
		flag.Usage()
		return 1
	}

	if err != nil {
		fmt.Fprintf(os.Stderr, "%s\n", err)
		return 1
	}

	return 0
}

func main() {
	// we want to use defer in main, but os.Exit doesn't run defers, so...
	os.Exit(doMain())
}
