// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

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

// NewGit returns a Git object that is used to interface with the external
// git tool. It can be used to retrieve the URL for a git repository, for
// example. The path to the external binary is assumed to be in the user
// environment, and is found using exec.LookPath("git"). NewGit will
// return an error if git is not in the current path.
func NewGit() (*Git, error) {
	path, err := exec.LookPath("git")
	if err != nil {
		return nil, err
	}

	return &Git{
		gitPath: path,
	}, nil
}

// Return the URL of the git repository where "dir" lives. Calls out to
// the external git executable.
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

// Return the commit hash of the git repository where "dir" lives. Calls out to
// the external git executable.
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
