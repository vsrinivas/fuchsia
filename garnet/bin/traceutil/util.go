// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"os"
	"os/exec"
	"path"
)

var fuchsiaRoot = getFuchsiaRoot()
var buildRoot = getBuildRoot(fuchsiaRoot)

func runCommand(command string, args []string) error {
	cmd := exec.Command(command)
	cmd.Args = append(cmd.Args, args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
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
	ext := path.Ext(filename)
	if ext == ".gz" {
		ext = path.Ext(filename[0:len(filename)-len(ext)]) + ext
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

	dir, _ := path.Split(execPath)
	for dir != "" && dir != "/" {
		dir = path.Clean(dir)
		manifestPath := path.Join(dir, ".jiri_manifest")
		if _, err = os.Stat(manifestPath); !os.IsNotExist(err) {
			return dir
		}
		dir, _ = path.Split(dir)
	}

	panic("Can not determine Fuchsia source root based on executable path.")
}

func getTraceutilBuildDir() string {
	execPath, err := os.Executable()
	if err != nil {
		panic(err.Error())
	}
	dir, _ := path.Split(execPath)
	return dir
}

func getBuildRoot(fxRoot string) string {
	execPath := getTraceutilBuildDir()

	outPath := path.Join(fxRoot, "out")
	dir, file := path.Split(execPath)
	for dir != "" && dir != "/" {
		dir = path.Clean(dir)
		if dir == outPath {
			return path.Join(dir, file)
		}
		dir, file = path.Split(dir)
	}

	panic("Can not determine output directory based on executable path.")
}

func getZirconBuildRoot() string {
	return buildRoot + ".zircon"
}

func getJsonGenerator() string {
	return path.Join(path.Dir(os.Args[0]), "trace2json")
}

func getExternalReportGenerator(reportType string) string {
	return path.Join(getTraceutilBuildDir(),
		"traceutil-generate-"+reportType)
}
