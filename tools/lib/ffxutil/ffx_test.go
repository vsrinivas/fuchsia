// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestFFXInstance(t *testing.T) {
	tmpDir := t.TempDir()
	ffxPath := filepath.Join(tmpDir, "ffx")
	if err := ioutil.WriteFile(ffxPath, []byte("#!/bin/bash\necho $@"), os.ModePerm); err != nil {
		t.Fatal("failed to write mock ffx tool")
	}
	ffx, _ := NewFFXInstance(ffxPath, tmpDir, []string{}, "target", filepath.Join(tmpDir, "sshKey"), filepath.Join(tmpDir, "out"))

	var buf []byte
	stdout := bytes.NewBuffer(buf)
	ffx.SetStdoutStderr(stdout, stdout)

	ctx := context.Background()

	assertRunsExpectedCmd := func(runErr error, expectedCmd string) {
		if runErr != nil {
			t.Errorf("failed to run cmd: %s", runErr)
		}
		stdoutStr := stdout.String()
		if !strings.HasSuffix(strings.TrimSpace(stdoutStr), fmt.Sprintf("--config %s %s", ffx.configPath, expectedCmd)) {
			t.Errorf("got %q, want %q", stdoutStr, expectedCmd)
		}
	}
	assertRunsExpectedCmd(ffx.List(ctx), "target list")

	assertRunsExpectedCmd(ffx.TargetWait(ctx), "--target target target wait")

	assertRunsExpectedCmd(
		ffx.Test(ctx, "test1", "-t", "30"),
		fmt.Sprintf(
			"--target target test run test1 --output-directory %s -t 30",
			filepath.Join(tmpDir, "out", "test_outputs"),
		),
	)

	// Snapshot expects a file to be written to tmpDir/snapshotZipName which it will move to tmpDir/new_snapshot.zip.
	if err := ioutil.WriteFile(filepath.Join(tmpDir, snapshotZipName), []byte("snapshot"), os.ModePerm); err != nil {
		t.Errorf("failed to write snapshot")
	}
	assertRunsExpectedCmd(ffx.Snapshot(ctx, tmpDir, "new_snapshot.zip"), "--target target target snapshot --dir "+tmpDir)
	if _, err := os.Stat(filepath.Join(tmpDir, snapshotZipName)); err == nil {
		t.Errorf("expected snapshot to be renamed")
	}
	if _, err := os.Stat(filepath.Join(tmpDir, "new_snapshot.zip")); err != nil {
		t.Errorf("failed to rename snapshot to new_snapshot.zip: %s", err)
	}

	assertRunsExpectedCmd(ffx.GetConfig(ctx), "config get")

	assertRunsExpectedCmd(ffx.Run(ctx, "random", "cmd", "with", "args"), "random cmd with args")

	assertRunsExpectedCmd(ffx.RunWithTarget(ctx, "random", "cmd", "with", "args"), "--target target random cmd with args")

	assertRunsExpectedCmd(ffx.Stop(), "daemon stop")
	if _, err := os.Stat(ffx.config.socket); !os.IsNotExist(err) {
		t.Errorf("failed to remove socket %s: %s", ffx.config.socket, err)
	}
}
