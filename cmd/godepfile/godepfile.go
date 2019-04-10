// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates a depfile for a Go package that can be consumed by Ninja.
package main

import (
	"flag"
	"fmt"
	"go/build"
	"log"
	"os"
	"runtime"
	"sort"
	"strings"
	"sync"
)

type stringsFlag []string

func (v *stringsFlag) String() string { return strings.Join(*v, " ") }

func (v *stringsFlag) Set(s string) error {
	*v = strings.Split(s, " ")
	if *v == nil {
		*v = []string{}
	}
	return nil
}

var (
	ctx    = build.Default
	output string
	test   bool
)

func init() {
	flag.Var((*stringsFlag)(&ctx.BuildTags), "tags", "build tags")
	flag.StringVar(&output, "o", "", "name of the resulting executable")
	flag.BoolVar(&test, "test", false, "whether this is a test target")

	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: godepfile [packages]\n")
		flag.PrintDefaults()
	}
}

func main() {
	flag.Parse()

	if len(flag.Args()) == 0 {
		flag.Usage()
		os.Exit(1)
	}

	var mu sync.Mutex
	deps := make(map[string]bool)
	paths := make(map[string]bool)

	fdlimit := make(chan struct{}, 128)
	var wg sync.WaitGroup
	var scan func(path, srcDir string)
	scan = func(path, srcDir string) {
		defer wg.Done()

		mu.Lock()
		_, done := paths[path]
		if !done {
			paths[path] = true
		}
		mu.Unlock()

		if done {
			return
		}

		if path == "C" {
			return
		}

		fdlimit <- struct{}{}
		defer func() { <-fdlimit }()

		pkg, err := ctx.Import(path, srcDir, 0)
		if err != nil {
			log.Fatalf("%s: %v", path, err)
		}

		var files []string
		srcdir := pkg.Dir + "/"
		files = appendAndPrefix(files, srcdir, pkg.GoFiles)
		files = appendAndPrefix(files, srcdir, pkg.CgoFiles)
		files = appendAndPrefix(files, srcdir, pkg.CFiles)
		files = appendAndPrefix(files, srcdir, pkg.CXXFiles)
		files = appendAndPrefix(files, srcdir, pkg.HFiles)
		files = appendAndPrefix(files, srcdir, pkg.SFiles)
		files = appendAndPrefix(files, srcdir, pkg.SwigFiles)
		files = appendAndPrefix(files, srcdir, pkg.SwigCXXFiles)

		if test {
			files = appendAndPrefix(files, srcdir, pkg.TestGoFiles)
			files = appendAndPrefix(files, srcdir, pkg.XTestGoFiles)
		}

		mu.Lock()
		for _, file := range files {
			deps[file] = true
		}
		mu.Unlock()

		if pkg.Name == "main" && output == "" {
			bindir := os.Getenv("GOBIN")
			if bindir == "" {
				bindir = pkg.BinDir
			}
			if ctx.GOOS == runtime.GOOS && ctx.GOARCH == runtime.GOARCH {
				output = ctx.JoinPath(bindir, path)
			} else {
				output = ctx.JoinPath(bindir, ctx.GOOS+"_"+ctx.GOARCH, path)
			}
		}

		for _, imp := range pkg.Imports {
			wg.Add(1)
			go scan(imp, pkg.Dir)
		}
	}

	for _, root := range flag.Args() {
		wg.Add(1)
		go scan(root, "")
	}
	wg.Wait()

	fmt.Printf("%s:", output)
	var depnames []string
	for path := range deps {
		depnames = append(depnames, path)
	}
	sort.Strings(depnames)
	for path, _ := range deps {
		fmt.Printf(" %s", path)
	}
	fmt.Printf("\n")
}

func appendAndPrefix(slice []string, prefix string, src []string) []string {
	for _, s := range src {
		slice = append(slice, prefix+s)
	}
	return slice
}
