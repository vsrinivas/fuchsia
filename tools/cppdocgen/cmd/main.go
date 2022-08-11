// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/docgen"
	"log"
	"os"
	"strings"
)

var flags struct {
	inDir          string
	outDir         string
	buildDir       string
	stripPathCount int
	repoBaseUrl    string
}

func init() {
	flag.StringVar(&flags.inDir, "indir", "", "Input directory")
	flag.StringVar(&flags.outDir, "outdir", "", "Output directory")
	flag.StringVar(&flags.buildDir, "build-dir", "", "Build directory from whence clang-doc paths are relative.")
	flag.StringVar(&flags.repoBaseUrl, "source-url", "", "URL of code repo for source links.")
	flag.IntVar(&flags.stripPathCount, "strip-include-elts", 0, "Strip this many path elements before header file names.")
}

func main() {
	flag.Parse()
	if len(flags.inDir) == 0 {
		log.Fatal("No input directory (-i) specified")
	}
	if len(flags.repoBaseUrl) == 0 {
		log.Fatal("No repo base URL (-u) specified")
	}
	if len(flags.buildDir) == 0 {
		log.Fatal("No build directory (--build-dir) specified")
	}
	if !strings.HasSuffix(flags.repoBaseUrl, "/") {
		// The base URL should always end in a slash for appending file paths.
		flags.repoBaseUrl += "/"
	}

	writeSettings := docgen.WriteSettings{
		LibName:           "fdio",
		StripPathEltCount: flags.stripPathCount,
		RepoBaseUrl:       flags.repoBaseUrl,
	}

	// All other args are the list of headers we want to index.
	indexSettings := docgen.IndexSettings{
		BuildDir: flags.buildDir,
		Headers:  make(map[string]struct{})}
	for _, a := range flag.Args() {
		indexSettings.Headers[a] = struct{}{}
	}

	if len(flags.outDir) == 0 {
		log.Fatal("No output directory (-o) specified")
	}
	err := os.MkdirAll(flags.outDir, 0o755)
	if err != nil {
		log.Fatal("Can't create output directory '%s':\n%v", flags.outDir, err)
	}

	root := clangdoc.Load(flags.inDir)

	index := docgen.MakeIndex(indexSettings, root)

	indexFile, err := os.OpenFile(flags.outDir+"/index.md", os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		log.Fatal(err)
	}
	docgen.WriteIndex(writeSettings, &index, indexFile)

	// Header references.
	for _, h := range index.Headers {
		n := h.ReferenceFileName()
		headerFile, err := os.OpenFile(flags.outDir+"/"+n, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0o644)
		if err != nil {
			log.Fatal(err)
		}
		docgen.WriteHeaderReference(writeSettings, &index, h, headerFile)
	}
}
