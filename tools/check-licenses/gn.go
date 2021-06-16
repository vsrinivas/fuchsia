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
