// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zbi

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type ZBITool struct {
	zbiToolPath string
	stdout      io.Writer
}

func NewZBITool(zbiToolPath string) (*ZBITool, error) {
	return NewZBIToolWithStdout(zbiToolPath, nil)
}

func NewZBIToolWithStdout(zbiToolPath string, stdout io.Writer) (*ZBITool, error) {
	if _, err := os.Stat(zbiToolPath); err != nil {
		return nil, err
	}
	return &ZBITool{
		zbiToolPath: zbiToolPath,
		stdout:      stdout,
	}, nil
}

func (z *ZBITool) MakeImageArgsZbi(ctx context.Context, destPath string, imageArgs map[string]string) error {
	path, err := exec.LookPath(z.zbiToolPath)
	if err != nil {
		return err
	}

	imageArgsFile, err := ioutil.TempFile("", "")
	if err != nil {
		return err
	}
	defer os.Remove(imageArgsFile.Name())

	for key, value := range imageArgs {
		if _, err := imageArgsFile.WriteString(fmt.Sprintf("%s=%s\n", key, value)); err != nil {
			return err
		}
	}

	args := []string{
		"--output",
		destPath,
		"--type",
		"IMAGE_ARGS",
		imageArgsFile.Name(),
	}

	logger.Infof(ctx, "running: %s %q", path, args)
	cmd := exec.CommandContext(ctx, path, args...)
	if z.stdout != nil {
		cmd.Stdout = z.stdout
	} else {
		cmd.Stdout = os.Stdout
	}
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
