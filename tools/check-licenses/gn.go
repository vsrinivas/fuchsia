// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package checklicenses

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"strings"
)

type Gn struct {
	gnPath string
	outDir string
}

// NewGn returns a GN object that is used to interface with the external GN tool. It can be used to
// discover the dependendcies of a GN target. The path to the external binary is taken from the
// config argument (config.GnPath.) NewGn will return an error if config.GnPath is not a valid
// executable, or if config.BuildDir does not exist.
func NewGn(ctx context.Context, config *Config) (*Gn, error) {
	gn := &Gn{}

	path, err := exec.LookPath(config.GnPath)
	if err != nil {
		return nil, err
	}

	if _, err := os.Stat(config.BuildDir); os.IsNotExist(err) {
		return nil, fmt.Errorf("out directory does not exist: %s", config.BuildDir)
	}

	gn.gnPath = path
	gn.outDir = config.BuildDir

	return gn, nil
}

// Return the dependencies of the given GN target. Calls out to the external GN executable.
// Dependencies are returned as an array of GN label strings, e.g. //root/dir:target_name(toolchain)
// Both the target name and toolchain are optional. See
// https://gn.googlesource.com/gn/+/HEAD/docs/reference.md#labels for more information.
func (gn *Gn) Dependencies(ctx context.Context, target string) ([]string, error) {
	args := []string{
		"desc",
		gn.outDir,
		target,
		"deps",
		"--all",
	}

	cmd := exec.CommandContext(ctx, gn.gnPath, args...)
	var output bytes.Buffer
	cmd.Stdout = &output
	cmd.Stderr = os.Stderr
	err := cmd.Run()
	if err != nil {
		return []string{}, err
	}

	result := output.String()
	result = strings.TrimSpace(result)
	return strings.Split(result, "\n"), nil
}

// Converts a GN label string (such as those returned by Dependencies) and strips any target names
// and toolchains, thereby returning the directory of the label.
func LabelToDirectory(label string) (string, error) {
	if !strings.HasPrefix(label, "//") {
		return "", fmt.Errorf("Label missing leading `//`: %s", label)
	}
	label = label[2:]

	location := label
	if strings.Contains(label, ":") {
		location = strings.SplitN(label, ":", 2)[0]
	}

	return location, nil
}
