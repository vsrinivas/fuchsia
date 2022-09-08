// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"errors"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"os"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_go/codegen"
	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type flagsDef struct {
	jsonPath             *string
	outputImplPath       *string
	outputRootForIDEPath *string
	outputPkgNamePath    *string
}

var flags = flagsDef{
	jsonPath: flag.String("json", "",
		"relative path to the FIDL intermediate representation."),
	outputImplPath: flag.String("output-impl", "",
		"output path for the generated Go implementation."),
	outputRootForIDEPath: flag.String(
		"output-root-for-ide",
		"",
		"directory within which to output a symlink to the generated Go implementation "+
			"in order to faciliate IDE cross-references.",
	),
	outputPkgNamePath: flag.String("output-pkg-name", "",
		"output path for the generated Go implementation."),
}

// valid returns true if the parsed flags are valid.
func (f flagsDef) valid() bool {
	return *f.jsonPath != ""
}

func printUsage() {
	program := path.Base(os.Args[0])
	message := `Usage: ` + program + ` [flags]

Go FIDL backend, used to generate Go bindings from JSON IR input (the
intermediate representation of a FIDL library).

Flags:
`
	fmt.Fprint(flag.CommandLine.Output(), message)
	flag.PrintDefaults()
}

func main() {
	flag.Usage = printUsage
	flag.Parse()
	if !flags.valid() {
		printUsage()
		os.Exit(1)
	}

	root, err := fidlgen.ReadJSONIr(*flags.jsonPath)
	if err != nil {
		log.Fatal(err)
	}

	generator := codegen.NewGenerator()
	tree := codegen.Compile(root)

	if outputImplPath := *flags.outputImplPath; outputImplPath != "" {
		if err := generator.GenerateImplFile(tree, outputImplPath); err != nil {
			log.Fatalf("Error generating impl file: %v", err)
		}

		if outputRootForIDEPath := *flags.outputRootForIDEPath; outputRootForIDEPath != "" {
			if err := makeIDEFriendlySymlinks(ideFriendlySymlinksArgs{
				outputImplPath:       outputImplPath,
				outputRootForIDEPath: outputRootForIDEPath,
				goPackageName:        tree.PackageName,
			}); err != nil {
				log.Fatal(err)
			}
		}
	}

	if outputPkgNamePath := *flags.outputPkgNamePath; outputPkgNamePath != "" {
		if err := generator.GeneratePkgNameFile(tree, outputPkgNamePath); err != nil {
			log.Fatalf("Error generating pkg-name file: %v", err)
		}
	}
}

type ideFriendlySymlinksArgs struct {
	outputImplPath       string
	outputRootForIDEPath string
	goPackageName        string
}

func makeIDEFriendlySymlinks(args ideFriendlySymlinksArgs) error {
	const permUserReadWriteExecute fs.FileMode = 0700

	ideOutputDir := filepath.Join(args.outputRootForIDEPath, args.goPackageName)
	if err := os.MkdirAll(ideOutputDir, permUserReadWriteExecute); err != nil {
		return fmt.Errorf("Error creating IDE-friendly directory layout: %w", err)
	}

	absOutputImplPath, err := filepath.Abs(args.outputImplPath)
	if err != nil {
		return fmt.Errorf(
			"Error getting absolute path of generated Go implementation (relative path is %q): %w",
			args.outputImplPath,
			err,
		)
	}

	absIDEOutputDir, err := filepath.Abs(ideOutputDir)
	if err != nil {
		return fmt.Errorf(
			"Error getting absolute path of IDE-friendly directory (relative path is %q): %w",
			ideOutputDir,
			err,
		)
	}

	symlinkPathname := filepath.Join(absIDEOutputDir, "impl.go")
	if err := os.Remove(symlinkPathname); err != nil && !errors.Is(err, os.ErrNotExist) {
		return fmt.Errorf("Error removing old symlink at %q: %w", symlinkPathname, err)
	}

	if err := os.Symlink(absOutputImplPath, symlinkPathname); err != nil {
		// Ignore os.ErrExist here because if multiple targets are generating the Go
		// impl in parallel, we may race with another fidlgen_go invocation trying
		// to create an identical symlink.
		if !errors.Is(err, os.ErrExist) {
			return fmt.Errorf(
				"Error adding symlink at %q to %q: %w",
				absOutputImplPath,
				symlinkPathname,
				err,
			)
		}
	}

	return nil
}
