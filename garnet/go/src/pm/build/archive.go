// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package build contains configuration structures and functions for building
// Fuchsia packages.
package build

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/pkg"
)

func Archive(cfg *Config, outputPath string) error {
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

	// create new fuchsia archive file in the output dir
	// named <output>.far if flag is provided, otherwise name-version.far
	if outputPath == "" {
		outputPath = filepath.Join(cfg.OutputDir, fmt.Sprintf("%s-%s", p.Name, p.Version))
	}
	outputFile, err := os.Create(outputPath + ".far")
	if err != nil {
		return err
	}
	defer outputFile.Close()
	return far.Write(outputFile, archiveFiles)
}
