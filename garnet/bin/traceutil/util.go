// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
)

var fuchsiaRoot = getFuchsiaRoot()
var buildRoot = getBuildRoot(fuchsiaRoot)

func runCommand(command string, args []string) error {
	cmd := exec.Command(command, args...)
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("%s failed.  Output:\n%s", command, output)
	}
	return nil
}

func getCommandOutput(command string, args ...string) (string, error) {
	cmd := exec.Command(command, args...)
	output, err := cmd.CombinedOutput()
	return string(output), err
}

func fullExt(filename string) string {
	ext := filepath.Ext(filename)
	if ext == ".gz" {
		ext = filepath.Ext(filename[0:len(filename)-len(ext)]) + ext
	}
	return ext
}

func replaceFilenameExt(filename string, newExt string) string {
	oldExt := fullExt(filename)
	return filename[0:len(filename)-len(oldExt)] + "." + newExt
}

func getFuchsiaRoot() string {
	execPath, err := os.Executable()
	if err != nil {
		panic(err.Error())
	}

	for dir := filepath.Dir(execPath); dir != "" && dir != "/"; dir = filepath.Dir(dir) {
		dir = filepath.Clean(dir)
		if _, err = os.Stat(filepath.Join(dir, ".jiri_manifest")); !os.IsNotExist(err) {
			return dir
		}
	}

	panic("Can not determine Fuchsia source root based on executable path.")
}

func getTraceutilBuildDir() string {
	execPath, err := os.Executable()
	if err != nil {
		panic(err.Error())
	}
	return filepath.Dir(execPath)
}

func getBuildRoot(fxRoot string) string {
	execPath := getTraceutilBuildDir()

	outPath := filepath.Join(fxRoot, "out")
	for dir, file := filepath.Split(execPath); dir != "" && dir != "/"; dir, file = filepath.Split(dir) {
		if dir = filepath.Clean(dir); dir == outPath {
			return filepath.Join(dir, file)
		}
	}

	panic("Can not determine output directory based on executable path.")
}

func getZirconBuildRoot() string {
	return buildRoot + ".zircon"
}

func getJsonGenerator() string {
	return filepath.Join(getTraceutilBuildDir(), "trace2json")
}

func getExternalReportGenerator(reportType string) string {
	return filepath.Join(getTraceutilBuildDir(), "traceutil-generate-"+reportType)
}
