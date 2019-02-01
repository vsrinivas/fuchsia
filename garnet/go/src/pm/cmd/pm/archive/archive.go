// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package archive implements the `pm archive` command
package archive

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/pkg"
)

const usage = `Usage: %s archive
construct a single .far representation of the package
`

// Run reads the configured package meta FAR and produces a whole-package
// archive including the metadata and the blobs.
func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("archive", flag.ExitOnError)

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}

	mfest, err := cfg.Manifest()
	if err != nil {
		return err
	}

	var archiveFiles = map[string]string{
		"meta.far": cfg.MetaFAR(),
	}

	mf, err := os.Open(cfg.MetaFAR())
	if err != nil {
		return err
	}
	defer mf.Close()
	fr, err := far.NewReader(mf)
	if err != nil {
		return err
	}

	pkgJSON, err := fr.ReadFile("meta/package")
	if err != nil {
		return err
	}

	var p pkg.Package
	if err := json.Unmarshal(pkgJSON, &p); err != nil {
		return err
	}

	cd, err := fr.ReadFile("meta/contents")
	if err != nil {
		return err
	}
	buf := bytes.NewBuffer(cd)
	for {
		var line string
		line, err = buf.ReadString('\n')
		nameMerkle := strings.SplitN(strings.TrimSpace(line), "=", 2)

		if len(nameMerkle) != 2 {
			if err == nil {
				continue
			}
			if err == io.EOF {
				break
			}
			return err
		}
		// add to the archive with the merkle name, from the source path in the
		// manifest
		sourcePath := mfest.Paths[nameMerkle[0]]
		if sourcePath == "" {
			continue
		}
		archiveFiles[nameMerkle[1]] = sourcePath
		if err != nil {
			break
		}
	}
	if err != io.EOF {
		return err
	}

	// create new name-version.far archive in output dir
	outputFile, err := os.Create(filepath.Join(cfg.OutputDir, fmt.Sprintf("%s-%s.far", p.Name, p.Version)))
	if err != nil {
		return err
	}
	defer outputFile.Close()
	if err := far.Write(outputFile, archiveFiles); err != nil {
		return err
	}

	return err
}
