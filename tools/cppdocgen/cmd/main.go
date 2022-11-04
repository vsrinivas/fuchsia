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
	"path/filepath"
	"strings"
)

var flags struct {
	inDir        string
	inZip        string
	outDir       string
	outZip       string
	libName      string
	sourceRoot   string
	buildDir     string
	includeDir   string
	repoBaseUrl  string
	tocPath      string
	overviewFile string
}

func init() {
	flag.StringVar(&flags.inDir, "in-dir", "", "Input directory")
	flag.StringVar(&flags.inZip, "in-zip", "", "Input zip file")
	flag.StringVar(&flags.outDir, "out-dir", "", "Output directory")
	flag.StringVar(&flags.outZip, "out-zip", "", "Output zip file")
	flag.StringVar(&flags.libName, "lib-name", "", "User-visible library name")
	flag.StringVar(&flags.sourceRoot, "source-root", "", "Repository root directory.")
	flag.StringVar(&flags.buildDir, "build-dir", "",
		"Build directory from whence clang-doc paths are relative.")
	flag.StringVar(&flags.includeDir, "include-dir", "",
		"The directory where #includes are relative to (used for generating titles and #include examples).")
	flag.StringVar(&flags.repoBaseUrl, "source-url", "",
		"URL of code repo for paths will be appended to for generating source links.")
	flag.StringVar(&flags.tocPath, "toc-path", "",
		"Absolute path on devsite where this code will be hosted, for paths in _toc.yaml.")
	flag.StringVar(&flags.overviewFile, "overview", "",
		"Path of the file that will comprise the top of the index.")
}

func main() {
	flag.Parse()
	if len(flags.repoBaseUrl) == 0 {
		log.Fatal("No repo base URL (-u) specified")
	}
	if !strings.HasSuffix(flags.repoBaseUrl, "/") {
		// The base URL should always end in a slash for appending file paths.
		flags.repoBaseUrl += "/"
	}

	if len(flags.libName) == 0 {
		log.Fatal("No library name (--lib-name) specified")
	}
	if len(flags.sourceRoot) == 0 {
		log.Fatal("No respository source root (--source-root) specified")
	}
	if len(flags.buildDir) == 0 {
		log.Fatal("No build directory (--build-dir) specified")
	}
	if len(flags.includeDir) == 0 {
		log.Fatal("No include directory (--include-dir) specified")
	}

	tocPath := flags.tocPath
	if len(tocPath) == 0 {
		log.Fatal("No --toc-path specified")
	}
	if !strings.HasSuffix(tocPath, "/") {
		tocPath += "/"
	}

	var overviewContents []byte
	if flags.overviewFile != "" {
		inContents, err := os.ReadFile(flags.overviewFile)
		if err != nil {
			log.Fatal(err)
		}
		overviewContents = inContents
	}

	buildRelSourceRoot, err := filepath.Rel(flags.buildDir, flags.sourceRoot)
	if err != nil {
		log.Fatal("Can't rebase source root: %s", err)
	}
	buildRelIncludeDir, err := filepath.Rel(flags.buildDir, flags.includeDir)
	if err != nil {
		log.Fatal("Can't rebase include dir: %s", err)
	}
	writeSettings := docgen.WriteSettings{
		LibName:            flags.libName,
		BuildRelSourceRoot: buildRelSourceRoot,
		BuildRelIncludeDir: buildRelIncludeDir,
		RepoBaseUrl:        flags.repoBaseUrl,
		TocPath:            tocPath,
		OverviewContents:   overviewContents,
	}

	// All other args are the list of headers we want to index.
	indexSettings := docgen.MakeIndexSettings(flags.buildDir)
	for _, a := range flag.Args() {
		indexSettings.Headers[a] = struct{}{}
	}

	// Validate input flags.
	if flags.inZip != "" && flags.inDir != "" {
		log.Fatal("Can't specify both --in-dir and --in-zip")
	} else if flags.inZip == "" && flags.inDir == "" {
		log.Fatal("Must specify either --in-dir=<dir> or --in-zip=<filename>.zip")
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
			file, err := os.Create(flags.outDir + "/" + name)
			if err != nil {
				log.Fatal(err)
			}
			return file
		}
	}

	var root *clangdoc.NamespaceInfo
	if flags.inDir != "" {
		root = clangdoc.LoadDir(flags.inDir)
	} else {
		root = clangdoc.LoadZip(flags.inZip)
	}

	index := docgen.MakeIndex(indexSettings, root)
	indexFile := addFile("index.md")
	docgen.WriteIndex(writeSettings, &index, indexFile)
	tocFile := addFile("_toc.yaml")
	docgen.WriteToc(writeSettings, &index, tocFile)

	// Header references.
	for _, h := range index.Headers {
		n := h.ReferenceFileName()
		headerFile := addFile(n)
		docgen.WriteHeaderReference(writeSettings, &index, h, headerFile)
	}
}
