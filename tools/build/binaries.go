// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package build

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
)

var (
	// ErrBuildIDNotFound is a sometimes expected error that a build ID was not found.
	ErrBuildIDNotFound = errors.New("build ID not found")
)

// Binary represents a binary produced by the build.
type Binary struct {
	// CPU is the CPU architecture of the binary.
	CPU string `json:"cpu"`

	// OS is the operating system that the binary is for.
	OS string `json:"os"`

	// Environment is the toolchain environment name of what specific execution
	// this was built for, e.g. "user", "host", "guest".
	Environment string `json:"environment"`

	// Label is the GN label of the binary target.
	Label string `json:"label"`

	// Type is the type of the binary.
	Type string `json:"type"`

	// Debug is the path to the unstripped or separate debug file is.
	Debug string `json:"debug"`

	// Dist is the path to the stripped binary for deployment/execution.
	Dist string `json:"dist,omitempty"`

	// BuildIDFile is the path to a file containing the lowercase ASCII
	// hexadecimal representation of the ELF build ID.
	BuildIDFile string `json:"elf_build_id,omitempty"`

	// Breakpad is the path to the breakpad symbol file for the debug binary, if
	// present.
	Breakpad string `json:"breakpad,omitempty"`
}

// ELFBuildID returns the build ID associated to a given binary. If no
// associated ID can be found - and no error unexpected error was experienced -
// ErrBuildIDNotFound is returned.
// build.GetBuildID() differs from elflib.GetBuildIDs() in that that former
// more statically determines the ID from build metadata, while the latter
// derives it directly from the ELF's notes section.
func (binary Binary) ELFBuildID(buildDir string) (string, error) {
	if binary.BuildIDFile != "" {
		p, err := filepath.Abs(filepath.Join(buildDir, binary.BuildIDFile))
		if err != nil {
			return "", err
		}
		f, err := os.Open(p)
		if err == nil {
			defer f.Close()
			content, err := ioutil.ReadAll(f)
			if err != nil {
				return "", fmt.Errorf("failed to read binary's build ID file: %v", err)
			}
			return strings.TrimSpace(string(content)), nil
		} else if !os.IsNotExist(err) {
			// It is WAI that there are binary entries that don't correspond to
			// anything that was actually built (i.e., was in the GN graph, but
			// not in the Ninja graph or was not a prebuilt). In this case, we
			// fall through.
			return "", err
		}
	}

	// If no elf_build_id is present, then it might be that the binary is a
	// prebuilt, as the former is a detail of the build. In this case, the
	// path to the debug binary should be in ".build-id" format, from which one
	// can easily derive the associated build ID. This format is
	// `.../<first two characters of buildID>/<remaining characters>.debug`.
	tokens := strings.Split(binary.Debug, string(os.PathSeparator))
	l := len(tokens)
	if l >= 3 && tokens[l-3] == ".build-id" {
		// First two characters + the rest.
		return tokens[l-2] + trimExt(filepath.Base(tokens[l-1])), nil
	}

	return "", ErrBuildIDNotFound
}

// LoadBinaries reads in the entries indexed in the given binary manifest.
func loadBinaries(manifest string) ([]Binary, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %v", manifest, err)
	}
	defer f.Close()
	var bins []Binary
	if err := json.NewDecoder(f).Decode(&bins); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %v", manifest, err)
	}
	return bins, nil
}

func trimExt(p string) string {
	return p[:len(p)-len(filepath.Ext(p))]
}
