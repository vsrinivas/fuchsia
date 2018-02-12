// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"application/lib/app/context"
	"fidl/bindings"
	"flag"
	"fmt"
	"garnet/amber/api/amber"
	"os"
	"strings"
)

const usage = `usage: amber_ctl <command> [opts]
Commands
    get_up    - get an update for a package
      Options
        -n: name of the package
        -v: version of the package, if not supplied the latest is retrieved

    get_blob  - get the specified content blob
        -i: content ID of the blob

    add_src   - add a source to the list we can use
        -s: location of the package source
        -k: root key for the source, either a file or http[s] URL or the key
            itself
        -h: hash of the key, required regardless of how the key is supplied
        -l: minimum allowable time between requests to the source, in seconds

    rm_src    - remove a source
        -s: location of the source

    list_srcs - list the set of sources we can use

    check     - query the list of sources for updates to any of the regularly
                monitored packages
`

var (
	fs         = flag.NewFlagSet("default", flag.ExitOnError)
	pkgName    = fs.String("n", "", "Name of a package")
	pkgVersion = fs.String("v", "", "Version of a package")
	srcUrl     = fs.String("s", "", "The location of a package source")
	rateLimit  = fs.Int("l", 0, "Minimum time between requests to a source, in seconds")
	srcKey     = fs.String("k", "", "Root key for the source, this can be either the key itself or a http[s]:// or file:// URL to the key")
	srcKeyHash = fs.String("h", "", "SHA256 of the key. This is required whether the key is provided directly or by URL")
	blobID     = fs.String("i", "", "Content ID of the blob")
)

func doTest(pxy *amber.Control_Proxy) {
	v := int32(42)
	resp, err := pxy.DoTest(v)
	if err != nil {
		fmt.Println(err)
	} else {
		fmt.Printf("Response: %s\n", resp)
	}
}

func connect(ctx *context.Context) (*amber.Control_Proxy, amber.Control_Request) {
	var pxy *amber.Control_Proxy
	req, pxy := pxy.NewRequest(bindings.GetAsyncWaiter())
	ctx.ConnectToEnvService(req)
	return pxy, req
}

func main() {
	if len(os.Args) < 2 {
		fmt.Printf("Error: no command provided\n%s", usage)
		os.Exit(-1)
	}

	fs.Parse(os.Args[2:])

	proxy, _ := connect(context.CreateFromStartupInfo())

	switch os.Args[1] {
	case "get_up":
		// the amber daemon wants package names that start with "/", if not present
		// add this as a prefix
		if strings.Index(*pkgName, "/") != 0 {
			*pkgName = fmt.Sprintf("/%s", *pkgName)
		}
		blobID, err := proxy.GetUpdate(*pkgName, pkgVersion)
		if err == nil {
			fmt.Printf("Wrote update to blob %s\n", *blobID)
		} else {
			fmt.Printf("Error getting update %s\n", err)
		}
	case "get_blob":
		if err := proxy.GetBlob(*blobID); err != nil {
			fmt.Printf("Error getting content blob %s\n", err)
		}
	case "add_src":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "rm_src":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "list_srcs":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "check":
		fmt.Printf("%q not yet supported\n", os.Args[1])
	case "test":
		doTest(proxy)
	default:
		fmt.Printf("Error, %q is not a recognized command\n%s",
			os.Args[1], usage)
		os.Exit(-1)
	}

	proxy.Close()
}
