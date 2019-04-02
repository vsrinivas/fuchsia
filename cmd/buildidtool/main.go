// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"crypto/rand"
	"encoding/hex"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/tools/color"
	"fuchsia.googlesource.com/tools/elflib"
	"fuchsia.googlesource.com/tools/logger"
)

var (
	buildIDDir string
	stamp      string
	depFile    string
	extension  string
	colors     color.EnableColor
	level      logger.LogLevel
)

func init() {
	colors = color.ColorAuto
	level = logger.FatalLevel

	flag.StringVar(&buildIDDir, "build-id-dir", "", "path to .build-id dirctory")
	flag.StringVar(&stamp, "stamp", "", "path to stamp file which acts as a stand in for the .build-id file")
	flag.StringVar(&depFile, "dep-file", "", "path to dep file which tells the build system about the .build-id file")
	flag.StringVar(&extension, "extension", "", "suffix appended to the end of the file")
	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
}

func getTmpFile(path string, name string) (string, error) {
	out := make([]byte, 16)
	if _, err := rand.Read(out); err != nil {
		return "", nil
	}
	return filepath.Join(path, name+"-"+hex.EncodeToString(out)) + ".tmp", nil
}

func atomicLink(from, to string) error {
	// First make sure the directory already exists
	if err := os.MkdirAll(filepath.Dir(to), 0700); err != nil {
		return err
	}
	// Make a tmpFile in the same directory as 'from'
	dir, file := filepath.Split(from)
	tmpFile, err := getTmpFile(dir, file)
	if err != nil {
		return err
	}
	if err := os.Link(from, tmpFile); err != nil {
		return err
	}
	if err := os.Rename(tmpFile, to); err != nil {
		return err
	}
	// If "tmpFile" and "to" are already links to the same inode Rename does not remove tmpFile.
	if err := os.Remove(tmpFile); !os.IsNotExist(err) {
		return err
	}
	return nil
}

func atomicWrite(file, fmtStr string, args ...interface{}) error {
	dir, base := filepath.Split(file)
	tmpFile, err := getTmpFile(dir, base)
	if err != nil {
		return err
	}
	f, err := os.Create(tmpFile)
	if err != nil {
		return err
	}
	defer f.Close()
	_, err = fmt.Fprintf(f, fmtStr, args...)
	if err != nil {
		return err
	}
	return os.Rename(tmpFile, file)
}

func removeOldFile(newBuildID string) error {
	data, err := ioutil.ReadFile(stamp)
	if err != nil {
		if !os.IsNotExist(err) {
			return err
		}
		return nil
	}
	oldBuildID := string(data)
	// We don't want to remove what we just added!
	if newBuildID == oldBuildID {
		return nil
	}
	oldPath := filepath.Join(buildIDDir, oldBuildID[:2], oldBuildID[2:]) + extension
	// If the file has already been removed (perhaps by another process) then
	// just keep going.
	if err := os.Remove(oldPath); !os.IsNotExist(err) {
		return err
	}
	return nil
}

func main() {
	l := logger.NewLogger(level, color.NewColor(colors), os.Stderr, os.Stderr)
	// Parse flags and check for errors.
	flag.Parse()
	if buildIDDir == "" {
		l.Fatalf("-build-id-dir is required.")
	}
	if stamp == "" {
		l.Fatalf("-stamp file is required.")
	}
	if depFile == "" {
		l.Fatalf("-dep-file is required.")
	}
	args := flag.Args()
	if len(args) != 1 {
		l.Fatalf("exactly one binary must be given. no more. no less.")
	}
	file := args[0]
	// Get the build IDs
	f, err := os.Open(file)
	if err != nil {
		l.Fatalf("opening %s to read build id: %v", file, err)
	}
	defer f.Close()
	buildIDs, err := elflib.GetBuildIDs(file, f)
	if err != nil {
		l.Fatalf("reading build ID from %s: %v", file, err)
	}
	if len(buildIDs) != 1 {
		l.Fatalf("unexpected number of build IDs")
	}
	if len(buildIDs[0]) < 2 {
		l.Fatalf("build ID is too short")
	}
	// Get the buildID string
	buildID := []rune(hex.EncodeToString(buildIDs[0]))
	buildIDPath := filepath.Join(buildIDDir, string(buildID[:2]), string(buildID[2:])) + extension
	// Now perform the operations of the tool. The order in which these operations occur
	// ensures that, from the perspective of the build system, all these operations occur
	// atomically. This order is "valid" because unless the tool runs to the end
	// then ninja will rerun the step and when the step is rerun once finished the end
	// state will be valid. The order of the first 3 steps doesn't matter much but the
	// stamp file must be emitted last.
	if err = atomicLink(file, buildIDPath); err != nil {
		l.Fatalf("atomically linking %s to %s: %v", file, buildIDPath, err)
	}
	buildIDString := string(buildID)
	if err = removeOldFile(buildIDString); err != nil {
		l.Fatalf("removing old file referenced by %s: %v", stamp, err)
	}
	// Emit the dep file
	if err = atomicWrite(depFile, "%s: %s", stamp, buildIDPath); err != nil {
		l.Fatalf("emitting dep file %s: %v", depFile, err)
	}
	// Update the stamp
	if err = atomicWrite(stamp, buildIDString); err != nil {
		l.Fatalf("emitting final stamp %s: %v", stamp, err)
	}
}
