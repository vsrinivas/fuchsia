// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"time"
)

const serverBase = "amber-files"

var (
	usage  = "usage: amber-serve -d=<directory_path>"
	srcDir = flag.String("d", os.Getenv("FUCHSIA_BUILD_DIR"), "The path to the file repository to serve.")
	listen = flag.String("l", ":8083", "HTTP listen address")
	quiet  = flag.Bool("q", false, "Don't print out information about requests")
)

type loggingWriter struct {
	http.ResponseWriter
	status int
}

func (lw *loggingWriter) WriteHeader(status int) {
	lw.status = status
	lw.ResponseWriter.WriteHeader(status)
}

func main() {
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}
	flag.Parse()

	if *srcDir == "" {
		fmt.Println("The FUCHSIA_BUILD_DIR environment variable should be set or supply a path with -d")
	}

	if _, e := os.Stat(*srcDir); e != nil {
		log.Fatalf("Couldn't access respository directory %v\n", e)
		return
	}

	repoDir := filepath.Join(*srcDir, serverBase, "repository")

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		lw := &loggingWriter{w, 0}
		http.ServeFile(lw, r, filepath.Join(repoDir, r.URL.Path))
		if !*quiet {
			currentTime := time.Now()
			fmt.Printf("%s [amber-srv] %d %s\n",
				currentTime.Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
		}
	})

	log.Fatal(http.ListenAndServe(*listen, nil))
}
