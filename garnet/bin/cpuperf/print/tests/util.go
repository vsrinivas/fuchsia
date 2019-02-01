// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dje): This file exists to keep the build happy.
// It needs to see a .go file that isn't foo_test.go.
// Since we need to have this file we put some useful stuff here.

package tests

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"runtime"
)

var (
	fuchsiaRoot = getFuchsiaRoot()
	buildRoot   = getBuildRoot(fuchsiaRoot)
)

// TODO(dje): Move into a common package, use by traceutil too.
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

func getProgramBuildDir() string {
	execPath, err := os.Executable()
	if err != nil {
		panic(err.Error())
	}
	dir, _ := path.Split(execPath)
	return dir
}

func getBuildRoot(fxRoot string) string {
	execPath := getProgramBuildDir()

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

// Return (effectively) $FUCHSIA_DIR/out/$ARCH/host_$ARCH.
func getHostBuildDir() string {
	arch := ""
	switch runtime.GOARCH {
	case "amd64":
		arch = "x64"
	case "arm64":
		arch = "arm64"
	default:
		panic(fmt.Errorf("unknown GOARCH: %s", runtime.GOARCH))
	}
	return path.Join(buildRoot, "host_"+arch)
}

func runCommandWithOutputToFile(command string, args []string,
	output io.Writer) error {
	// This doesn't use testing.Logf or some such because we always
	// want to see this, especially when run on bots.
	fmt.Printf("Running %s %v\n", command, args)
	cmd := exec.Command(command)
	cmd.Args = append(cmd.Args, args...)
	// There's no point to distinguishing stdout,stderr here.
	cmd.Stdout = output
	cmd.Stderr = output
	err := cmd.Run()
	if err != nil {
		fmt.Printf("Running %s failed: %s\n", command, err.Error())
	}
	return err
}

func compareFiles(file1, file2 string) error {
	contents1, err := ioutil.ReadFile(file1)
	if err != nil {
		return fmt.Errorf("Unable to read %s: %s", file1, err.Error())
	}
	contents2, err := ioutil.ReadFile(file2)
	if err != nil {
		return fmt.Errorf("Unable to read %s: %s", file2, err.Error())
	}

	if bytes.Compare(contents1, contents2) != 0 {
		return fmt.Errorf("Match failure")
	}
	return nil
}
