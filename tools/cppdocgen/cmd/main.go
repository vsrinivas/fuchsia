// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/zip"
	"flag"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/clangdoc"
	"go.fuchsia.dev/fuchsia/tools/cppdocgen/docgen"
	"io"
	"log"
	"os"
	"strings"
)

var flags struct {
	inDir          string
	outDir         string
	outZip         string
	buildDir       string
	stripPathCount int
	repoBaseUrl    string
}

func init() {
	flag.StringVar(&flags.inDir, "in-dir", "", "Input directory")
	flag.StringVar(&flags.outDir, "out-dir", "", "Output directory")
	flag.StringVar(&flags.outZip, "out-zip", "", "Output zip file")
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

	// Validate output flags.
	if flags.outZip != "" && flags.outDir != "" {
		log.Fatal("Can't specify both --out-dir and --out-zip")
	} else if flags.outZip == "" && flags.outDir == "" {
		log.Fatal("Must specify either --out-dir=<dir> or --out-zip=<filename>.zip")
	}

	// Set up output. The addFile() lambda will create the given file in the output format
	// requested.
	var addFile func(string) io.Writer
	if len(flags.outZip) != 0 {
		// Create a zip file.
		zipFile, err := os.Create(flags.outZip)
		if err != nil {
			log.Fatal("Can't create output file '%s':\n%v", flags.outZip, err)
		}
		defer zipFile.Close()

		zipWriter := zip.NewWriter(zipFile)
		defer zipWriter.Close()

		addFile = func(path string) io.Writer {
			file, err := zipWriter.Create(path)
			if err != nil {
				log.Fatal("Can't add '%s' to zip file\n%v", path, err)
			}
			return file
		}
	} else {
		// Create a directory.
		err := os.MkdirAll(flags.outDir, 0o755)
		if err != nil {
			log.Fatal("Can't create output directory '%s':\n%v", flags.outDir, err)
		}
		addFile = func(name string) io.Writer {
			file, err := os.Create(name)
			if err != nil {
				log.Fatal(err)
			}
			return file
		}
	}

	root := clangdoc.Load(flags.inDir)

	index := docgen.MakeIndex(indexSettings, root)
	indexFile := addFile("index.md")
	docgen.WriteIndex(writeSettings, &index, indexFile)

	// Header references.
	for _, h := range index.Headers {
		n := h.ReferenceFileName()
		headerFile := addFile(n)
		docgen.WriteHeaderReference(writeSettings, &index, h, headerFile)
	}
}
