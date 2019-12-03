// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/botanist/target"
	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

func TestExecute(t *testing.T) {
	t.Run("close logs after cmd finishes", func(t *testing.T) {
		tmpDir, err := ioutil.TempDir("", fmt.Sprintf("test-data"))
		if err != nil {
			t.Fatalf("failed to create temp dir: %v", err)
		}
		defer os.RemoveAll(tmpDir)
		syslogPath := filepath.Join(tmpDir, "syslog.txt")
		serialLogPath := filepath.Join(tmpDir, "serial.txt")

		// create empty script to pass as cmd to run.
		scriptPath := filepath.Join(tmpDir, "test_script.sh")
		if err = ioutil.WriteFile(scriptPath, []byte("#!/bin/bash\n# don't do anything."), 0755); err != nil {
			t.Fatalf("Failed to write to script: %s", err)
		}

		cmd := &RunCommand{
			syslogFile:    syslogPath,
			netboot:       true,
			timeout:       time.Second * 5,
			serialLogFile: serialLogPath,
		}
		loggerOutFile := filepath.Join(tmpDir, "logger_out.txt")
		loggerOut, err := os.Create(loggerOutFile)
		if err != nil {
			t.Fatalf("failed to create log file: %v", err)
		}

		serialName := filepath.Join(tmpDir, "real_serial.txt")
		serialWriter, err := os.Create(serialName)
		if err != nil {
			t.Fatalf("failed to create mock serial writer: %v", err)
		}
		defer serialWriter.Close()
		testLogger := logger.NewLogger(logger.DebugLevel, color.NewColor(color.ColorAuto), loggerOut, loggerOut, "botanist ")
		ctx := logger.WithLogger(context.Background(), testLogger)
		var targets []Target
		for i := 0; i < 2; i++ {
			var serial io.ReadWriteCloser
			if i == 0 {
				serial = serialWriter
			}
			target, err := target.NewMockTarget(ctx, fmt.Sprintf("mock-target-%d", i+1), serial)
			if err != nil {
				t.Fatalf("failed to create new target: %v", err)
			}
			targets = append(targets, target)
		}

		// cmd.execute() calls setupTargets() followed by runCmdWithTargets().
		// The logs should be open for writing to when runCmdWithTargets()
		// is called and closed after it finishes.
		var imgs []build.Image
		targetSetup := cmd.setupTargets(ctx, imgs, targets)
		for _, log := range targetSetup.syslogs {
			if _, err := io.WriteString(log.file, "File is open!"); err != nil {
				t.Fatalf("File is not open: %v", err)
			}
		}
		if err = cmd.runCmdWithTargets(ctx, targetSetup, []string{scriptPath}); err != nil {
			t.Fatalf("Execute failed with error: %v", err)
		}
		for _, log := range targetSetup.syslogs {
			if _, err := io.WriteString(log.file, "File is closed!"); err == nil {
				t.Fatalf("File is open: %v", err)
			}
		}

		loggerOut.Close()
		log, err := ioutil.ReadFile(loggerOutFile)
		if err != nil {
			t.Fatalf("%v", err)
		}
		if strings.Contains(string(log), "serial output not closed yet") {
			t.Fatalf("Failed to close syslog/serial logs after stopping the target.")
		}
	})

	t.Run("fail for empty logs", func(t *testing.T) {
		tmpDir, err := ioutil.TempDir("", fmt.Sprintf("test-data"))
		if err != nil {
			t.Fatalf("failed to create temp dir: %v", err)
		}
		defer os.RemoveAll(tmpDir)
		syslogPath := filepath.Join(tmpDir, "syslog.txt")

		// create empty script to pass as cmd to run.
		scriptPath := filepath.Join(tmpDir, "test_script.sh")
		if err = ioutil.WriteFile(scriptPath, []byte("#!/bin/bash\n# don't do anything."), 0755); err != nil {
			t.Fatalf("Failed to write to script: %s", err)
		}

		cmd := &RunCommand{
			syslogFile: syslogPath,
			netboot:    true,
			timeout:    time.Second * 5,
		}
		ctx := context.Background()
		target, err := target.NewMockTarget(ctx, "mock-target", nil)
		if err != nil {
			t.Fatalf("failed to create new target: %v", err)
		}
		targets := []Target{target}

		var imgs []build.Image
		targetSetup := cmd.setupTargets(ctx, imgs, targets)
		if err = cmd.runCmdWithTargets(ctx, targetSetup, []string{scriptPath}); err != nil {
			t.Fatalf("Execute failed with error: %v", err)
		}
		// Logs should be empty.
		if err = checkEmptyLogs(ctx, append(targetSetup.syslogs, targetSetup.serialLogs...)); err == nil {
			t.Fatalf("Did not fail for empty logs")
		}
	})
}
