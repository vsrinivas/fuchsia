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
	"strings"

	"fuchsia.googlesource.com/tools/color"
	"fuchsia.googlesource.com/tools/elflib"
	"fuchsia.googlesource.com/tools/logger"
)

type entry struct {
	suffix string
	file   string
}

type entryList []entry

func (a *entryList) String() string {
	return fmt.Sprintf("%v", []entry(*a))
}

func (a *entryList) Set(value string) error {
	args := strings.SplitN(value, "=", 2)
	if len(args) != 2 {
		return fmt.Errorf("'%s' is not a valid entry. Must be in format <suffix>=<file>", value)
	}
	*a = append(*a, entry{args[0], args[1]})
	return nil
}

var (
	buildIDDir string
	stamp      string
	entries    entryList
	colors     color.EnableColor
	level      logger.LogLevel
)

func init() {
	colors = color.ColorAuto
	level = logger.FatalLevel

	flag.StringVar(&buildIDDir, "build-id-dir", "", "path to .build-id dirctory")
	flag.StringVar(&stamp, "stamp", "", "path to stamp file which acts as a stand in for the .build-id file")
	flag.Var(&entries, "entry", "supply <suffix>=<file> to link <file> into .build-id with the given suffix")
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

func removeOldFile(newBuildID, suffix string) error {
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
	oldPath := filepath.Join(buildIDDir, oldBuildID[:2], oldBuildID[2:]) + suffix
	// If the file has already been removed (perhaps by another process) then
	// just keep going.
	if err := os.Remove(oldPath); !os.IsNotExist(err) {
		return err
	}
	return nil
}

type entryInfo struct {
	ref    elflib.BinaryFileRef
	suffix string
}

func getEntriesInfo() ([]entryInfo, error) {
	var outs []entryInfo
	for _, entry := range entries {
		f, err := os.Open(entry.file)
		if err != nil {
			return nil, fmt.Errorf("opening %s to read build ID: %v", entry.file, err)
		}
		defer f.Close()
		buildIDs, err := elflib.GetBuildIDs(entry.file, f)
		if err != nil {
			return nil, fmt.Errorf("reading build ID from %s: %v", entry.file, err)
		}
		if len(buildIDs) != 1 {
			return nil, fmt.Errorf("unexpected number of build IDs in %s. Expected 1 but found %v", entry.file, buildIDs)
		}
		if len(buildIDs[0]) < 2 {
			return nil, fmt.Errorf("build ID (%s) is too short in %s", buildIDs[0], entry.file)
		}
		buildID := hex.EncodeToString(buildIDs[0])
		outs = append(outs, entryInfo{elflib.BinaryFileRef{BuildID: buildID, Filepath: entry.file}, entry.suffix})
	}
	return outs, nil
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
	if len(entries) == 0 {
		l.Fatalf("Need at least one -entry arg")
	}
	// Get the build IDs
	infos, err := getEntriesInfo()
	if err != nil {
		l.Fatalf("Parsing entries: %v", err)
	}
	buildID := infos[0].ref.BuildID
	for _, info := range infos {
		if buildID != info.ref.BuildID {
			l.Fatalf("%s and %s do not have the same build ID", info.ref.Filepath, infos[0].ref.Filepath)
		}
		if err := info.ref.Verify(); err != nil {
			l.Fatalf("Could not verify build ID of %s: %v", info.ref.Filepath, err)
		}
	}
	// Now that we know all the build IDs are in order perform operations.
	// Make sure to not output the stamp file until all of these operations are
	// performed to ensure that this tool is re-run if it fails mid-run.
	buildIDRunes := []rune(buildID)
	buildIDPathPrefix := filepath.Join(buildIDDir, string(buildIDRunes[:2]), string(buildIDRunes[2:]))
	for _, info := range infos {
		buildIDPath := buildIDPathPrefix + info.suffix
		if err = atomicLink(info.ref.Filepath, buildIDPath); err != nil {
			l.Fatalf("atomically linking %s to %s: %v", info.ref.Filepath, buildIDPath, err)
		}
		if err = removeOldFile(buildID, info.suffix); err != nil {
			l.Fatalf("removing old file referenced by %s: %v", stamp, err)
		}
	}
	// Update the stamp last atomically to commit all the above operations.
	if err = atomicWrite(stamp, buildID); err != nil {
		l.Fatalf("emitting final stamp %s: %v", stamp, err)
	}
}
