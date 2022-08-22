// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

func TestFFXInstance(t *testing.T) {
	tmpDir := t.TempDir()
	ffxPath := filepath.Join(tmpDir, "ffx")
	if err := os.WriteFile(ffxPath, []byte("#!/bin/bash\necho $@"), os.ModePerm); err != nil {
		t.Fatal("failed to write mock ffx tool")
	}
	ffx, _ := NewFFXInstance(ffxPath, tmpDir, []string{}, "target", filepath.Join(tmpDir, "sshKey"), filepath.Join(tmpDir, "out"))

	var buf []byte
	stdout := bytes.NewBuffer(buf)
	ffx.SetStdoutStderr(stdout, stdout)

	ctx := context.Background()

	assertRunsExpectedCmd := func(runErr error, stdout *bytes.Buffer, expectedConfigPath, expectedCmd string) {
		if runErr != nil {
			t.Errorf("failed to run cmd: %s", runErr)
		}
		stdoutStr := stdout.String()
		if !strings.HasSuffix(strings.TrimSpace(stdoutStr), fmt.Sprintf("--config %s %s", expectedConfigPath, expectedCmd)) {
			t.Errorf("got %q, want %q", stdoutStr, expectedCmd)
		}
	}
	assertRunsExpectedCmd(ffx.List(ctx), stdout, ffx.ConfigPath, "target list")

	assertRunsExpectedCmd(ffx.TargetWait(ctx), stdout, ffx.ConfigPath, "--target target target wait")

	// Create a new instance that uses the same ffx config but runs against a different target.
	ffx2 := FFXInstanceWithConfig(ffxPath, tmpDir, []string{}, "target2", ffx.ConfigPath)
	var buf2 []byte
	stdout2 := bytes.NewBuffer(buf2)
	ffx2.SetStdoutStderr(stdout2, stdout2)
	assertRunsExpectedCmd(ffx2.TargetWait(ctx), stdout2, ffx.ConfigPath, "--target target2 target wait")

	// Test expects a run_summary.json to be written in the test output directory.
	outDir := filepath.Join(tmpDir, "out")
	testOutputDir := filepath.Join(outDir, "test-outputs")
	if err := os.MkdirAll(testOutputDir, os.ModePerm); err != nil {
		t.Errorf("failed to create test outputs dir: %s", err)
	}
	runSummaryBytes := []byte("{\"schema_id\": \"https://fuchsia.dev/schema/ffx_test/run_summary-8d1dd964.json\"}")
	if err := os.WriteFile(filepath.Join(testOutputDir, runSummaryFilename), runSummaryBytes, os.ModePerm); err != nil {
		t.Errorf("failed to write run_summary.json: %s", err)
	}
	_, err := ffx.Test(ctx, build.TestList{}, outDir)
	assertRunsExpectedCmd(
		err,
		stdout,
		ffx.ConfigPath,
		fmt.Sprintf(
			"--target target test run --continue-on-timeout --test-file %s --output-directory %s",
			filepath.Join(outDir, "test-list.json"), testOutputDir,
		),
	)

	// Snapshot expects a file to be written to tmpDir/snapshotZipName which it will move to tmpDir/new_snapshot.zip.
	if err := os.WriteFile(filepath.Join(tmpDir, snapshotZipName), []byte("snapshot"), os.ModePerm); err != nil {
		t.Errorf("failed to write snapshot")
	}
	assertRunsExpectedCmd(ffx.Snapshot(ctx, tmpDir, "new_snapshot.zip"), stdout, ffx.ConfigPath, "--target target target snapshot --dir "+tmpDir)
	if _, err := os.Stat(filepath.Join(tmpDir, snapshotZipName)); err == nil {
		t.Errorf("expected snapshot to be renamed")
	}
	if _, err := os.Stat(filepath.Join(tmpDir, "new_snapshot.zip")); err != nil {
		t.Errorf("failed to rename snapshot to new_snapshot.zip: %s", err)
	}

	assertRunsExpectedCmd(ffx.GetConfig(ctx), stdout, ffx.ConfigPath, "config get")

	assertRunsExpectedCmd(ffx.Run(ctx, "random", "cmd", "with", "args"), stdout, ffx.ConfigPath, "random cmd with args")

	assertRunsExpectedCmd(ffx.RunWithTarget(ctx, "random", "cmd", "with", "args"), stdout, ffx.ConfigPath, "--target target random cmd with args")

	config := make(map[string]interface{})
	for k, v := range ffx.Config.config {
		config[k] = v
	}
	if err := ffx.SetConfigJsonPointer("/key1/key2/key3", "value"); err != nil {
		t.Errorf("failed to set key1.key2.key3=value in config: %s", err)
	}
	if diff := diffConfig(config, ffx.Config.config); diff == "" {
		t.Errorf("failed to update config")
	}
	fileConfig, err := configFromFile(ffx.ConfigPath)
	if err != nil {
		t.Errorf("failed to get config from file: %s", err)
	}
	if diff := diffConfig(fileConfig.config, ffx.Config.config); diff != "" {
		t.Errorf("Got wrong config (-got +want):\n%s", diff)
	}
	if fileConfig.GetJsonPointer("/key1/key2/key3") != "value" {
		t.Errorf("failed to set key1.key2.key3=value in config: %v", fileConfig)
	}

	assertRunsExpectedCmd(ffx.Stop(), stdout, ffx.ConfigPath, "daemon stop")
	if _, err := os.Stat(ffx.Config.socket); !os.IsNotExist(err) {
		t.Errorf("failed to remove socket %s: %s", ffx.Config.socket, err)
	}
}
