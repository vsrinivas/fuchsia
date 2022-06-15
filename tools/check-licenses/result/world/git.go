// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

import (
	"bytes"
	"context"
	"os"
	"os/exec"
	"strings"
)

type Git struct {
	gitPath string
}

func NewGit() (*Git, error) {
	path, err := exec.LookPath("git")
	if err != nil {
		return nil, err
	}

	return &Git{
		gitPath: path,
	}, nil
}

func (git *Git) GetURL(ctx context.Context, dir string) (string, error) {
	var output bytes.Buffer
	args := []string{
		"config",
		"--get",
		"remote.origin.url",
	}

	cmd := exec.CommandContext(ctx, git.gitPath, args...)
	cmd.Dir = dir
	cmd.Stdout = &output
	cmd.Stderr = os.Stderr
	err := cmd.Run()
	if err != nil {
		return "", err
	}

	result := output.String()
	result = strings.TrimSpace(result)

	return result, nil
}

func (git *Git) GetCommitHash(ctx context.Context, dir string) (string, error) {
	var output bytes.Buffer
	args := []string{
		"rev-parse",
		"HEAD",
	}

	cmd := exec.CommandContext(ctx, git.gitPath, args...)
	cmd.Dir = dir
	cmd.Stdout = &output
	cmd.Stderr = os.Stderr
	err := cmd.Run()
	if err != nil {
		return "", err
	}

	result := output.String()
	result = strings.TrimSpace(result)

	return result, nil
}
