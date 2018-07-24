// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lg

import (
	"bufio"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

// check that the log file has the provided log lines.
func checkLogFile(t *testing.T, path string, lines []string) {
	f, err := os.Open(path)
	if err != nil {
		t.Fatalf("failed to open file: %s", err)
	}
	defer f.Close()

	i := 0
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		if i >= len(lines) {
			t.Fatalf("too many lines in the log %q: %d != %d", path, i, len(lines))
		}

		line := scanner.Text()
		if line != lines[i] {
			t.Fatalf("line %q:%d is wrong: %q != %q", path, i, line, lines[i])
		}
		i++
	}

	if err := scanner.Err(); err != nil {
		t.Fatalf("failed to read from file: %s", err)
	}

	if i != len(lines) {
		t.Fatalf("expected more lines in log file %q: %d != %d", path, i, len(lines))
	}
}

func TestFileLoggerWrite(t *testing.T) {
	dir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("failed to create tempfile")
	}
	defer os.RemoveAll(dir)

	path := filepath.Join(dir, "log")
	Log := NewFileLoggerWithOptions(path, FileLoggerOptions{
		MaxLogSize:     1024,
		MaxOldLogFiles: 5,
		Timestamp:      false,
	})

	var lines []string
	for i := 0; i < 10; i++ {
		Log.Infof("%03d", i)
		lines = append(lines, fmt.Sprintf("INFO: %03d", i))
	}

	if err := Log.Sync(); err != nil {
		t.Fatalf("failed to sync log file: %s", err)
	}

	// log.Printf appends a newline, so we slice that off to compare
	checkLogFile(t, path, lines)
}

func TestFileLoggerRotation(t *testing.T) {
	dir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("failed to create tempfile")
	}
	defer os.RemoveAll(dir)

	maxMsgCount := 5

	path := filepath.Join(dir, "log")

	// log line is: "INFO: 000-000\n"
	maxLogSize := 14*maxMsgCount + 1
	maxLogFiles := 2

	Log := NewFileLoggerWithOptions(path, FileLoggerOptions{
		MaxLogSize:     uint64(maxLogSize),
		MaxOldLogFiles: maxLogFiles,
		Timestamp:      false,
	})

	// make a list of all the files we will write to.
	paths := []string{path}
	for logCount := 0; logCount < maxLogFiles-1; logCount++ {
		paths = append(paths, fmt.Sprintf("%s.%d", path, logCount))
	}

	var logLines [][]string
	for logCount := 0; logCount < maxLogFiles; logCount++ {
		var lines []string

		for msgCount := 0; msgCount < maxMsgCount; msgCount++ {
			Log.Infof("%03d-%03d", logCount, msgCount)
			lines = append(lines, fmt.Sprintf("INFO: %03d-%03d", logCount, msgCount))
		}

		logLines = append(logLines, lines)
	}

	checkLogFile(t, paths[0], logLines[1])
	checkLogFile(t, paths[1], logLines[0])

	// Make sure we properly rotate.
	Log.Infof("hello")

	checkLogFile(t, paths[0], []string{"INFO: hello"})
	checkLogFile(t, paths[1], logLines[1])

	// Error out if we rotated more than we wanted to.
	if _, err := os.Stat(fmt.Sprintf("%s.2", path)); err == nil {
		t.Fatalf("created too many log files")
	}

}
