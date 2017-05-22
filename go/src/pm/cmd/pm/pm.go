// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"golang.org/x/crypto/ed25519"

	"fuchsia.googlesource.com/pm/cmd/pm/genkey"
	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/cmd/pm/seal"
	"fuchsia.googlesource.com/pm/cmd/pm/sign"
	"fuchsia.googlesource.com/pm/cmd/pm/update"
	"fuchsia.googlesource.com/pm/cmd/pm/verify"
)

const usage = `%s [command]
    init    - initialize a package meta directory in the standard form
    genkey  - generate a new private/public key pair
    update  - update the merkle roots in meta/contents
    sign    - sign a package with the given key
    seal    - seal package metadata into a signed meta.far
    verify  - verify metadata signature against the embedded public key
TODO:
    archive - construct a single .far representation of the package
    publish - upload the package to a distribution service
`

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		flag.PrintDefaults()
	}

	flag.Parse()

	d, err := os.Getwd()
	if err != nil {
		die(err)
	}

	switch flag.Arg(0) {
	case "init":
		err = initcmd.Run(d)

	case "genkey":
		err = genkey.Run(d)

	case "update":
		err = update.Run(d)

	case "sign":
		if flag.NArg() < 2 {
			die(fmt.Errorf("sign requires a private key file as an argument"))
		}
		buf, err := ioutil.ReadFile(flag.Arg(1))
		if err != nil {
			die(err)
		}
		err = sign.Run(d, ed25519.PrivateKey(buf))

	case "seal":
		err = seal.Run(d)

	case "verify":
		err = verify.Run(d)

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
