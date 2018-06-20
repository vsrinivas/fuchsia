// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/pm/build"
)

const (
	usage      = "usage: pm serve -d=<directory_path>"
	serverBase = "amber-files"
	maxLen     = 50
	trailLen   = 20
)

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	repoDir := fs.String("d", "", "The path to the file repository to serve.")
	listen := fs.String("l", ":8083", "HTTP listen address")
	quiet := fs.Bool("q", false, "Don't print out information about requests")

	fs.Usage = func() {
		fmt.Println(usage)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	if *repoDir == "" {
		if buildDir, ok := os.LookupEnv("FUCHSIA_BUILD_DIR"); ok {
			*repoDir = filepath.Join(buildDir, serverBase, "repository")
		} else {
			return fmt.Errorf("the FUCHSIA_BUILD_DIR environment variable should be set or supply a path with -d")
		}
	}

	fi, err := os.Stat(*repoDir)
	if err != nil {
		return fmt.Errorf("repository path %q is not valid: %s", *repoDir, err)
	}

	if !fi.IsDir() {
		return fmt.Errorf("repository path %q is not a directory", *repoDir)
	}

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if !*quiet {
			rStr := r.RequestURI
			if len(rStr) > maxLen {
				rStr = fmt.Sprintf("%s...%s", rStr[0:maxLen-trailLen-3], rStr[len(rStr)-trailLen:])
			}
			log.Printf("serving %q", rStr)
		}
		http.ServeFile(w, r, filepath.Join(*repoDir, r.URL.Path))
	})

	if !*quiet {
		log.Printf("starting server on %s", *listen)
	}

	return http.ListenAndServe(*listen, nil)
}
