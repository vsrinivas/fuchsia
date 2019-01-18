// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package snapshot contains the `pm snapshot` command
package snapshot

import (
	"bufio"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"fuchsia.googlesource.com/pm/build"
)

const usage = `Usage: %s snapshot
take a snapshot of one or more packages

A package entry is of the form:
name[#tag1[,tag2]*]=path/to/packages/blobs.json

A manifest must contain a single package entry per line
`

type packageEntry struct {
	Name      string
	BlobsPath string
	Tags      []string
}

//TODO(kevinwells) "_" is not allowed in package names, but some existing package names contain it.
var rePackageEntry = regexp.MustCompile("^" +
	"([a-z0-9._/-]+)" + // Package name and version
	"(?:#([^=]+))?" + // Optional tags
	"=" +
	"(.*)" + // Path to blobs manifest
	"$")

func parsePackageEntry(s string) (*packageEntry, error) {
	match := rePackageEntry.FindStringSubmatch(s)

	if len(match) != 4 {
		return nil, fmt.Errorf("'%v' is not a properly formatted package entry", s)
	}

	var tags []string
	if match[2] != "" {
		tags = strings.Split(match[2], ",")
		sort.Strings(tags)
	}

	return &packageEntry{
		match[1],
		match[3],
		tags,
	}, nil
}

type packageEntries []packageEntry

func (s *packageEntries) String() string {
	return fmt.Sprintf("%v", []packageEntry(*s))
}

func (s *packageEntries) Set(value string) error {
	entry, err := parsePackageEntry(value)
	if err != nil {
		return err
	}

	*s = append(*s, *entry)
	return nil
}

type snapshotConfig struct {
	packages     packageEntries
	manifestPath string
	outputPath   string
}

func parseConfig(args []string) (*snapshotConfig, error) {
	fs := flag.NewFlagSet("snapshot", flag.ExitOnError)

	var c snapshotConfig

	fs.StringVar(&c.manifestPath, "manifest", "", "The manifest of packages to include in the snapshot")
	fs.StringVar(&c.outputPath, "output", "", "The path of the output snapshot file")
	fs.Var(&c.packages, "package", "Add a package to the snapshot")

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return nil, err
	}

	if c.outputPath == "" {
		return nil, fmt.Errorf("output path required")
	}

	if c.manifestPath == "" && len(c.packages) == 0 {
		return nil, fmt.Errorf("either a manifest or package entries are required")
	}

	return &c, nil
}

func parseManifest(path string) ([]packageEntry, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	entries := []packageEntry{}
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		entry, err := parsePackageEntry(scanner.Text())
		if err != nil {
			return nil, err
		}
		entries = append(entries, *entry)
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return entries, nil
}

// loadBlobs attempts to read and parse a blobs manifest from the given path
func loadBlobs(path string) ([]build.PackageBlobInfo, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var members []build.PackageBlobInfo

	err = json.Unmarshal(data, &members)
	if err != nil {
		return nil, err
	}

	return members, nil
}

// buildSnapshot loads and aggregates package metadata requested by the command
// line into a single serializable struct
func buildSnapshot(c snapshotConfig) (*build.Snapshot, error) {
	snapshot := &build.Snapshot{
		make(map[string]build.Package),
		make(map[build.MerkleRoot]build.BlobInfo),
	}

	for _, pkgInfo := range c.packages {
		pkg := build.Package{
			make(map[string]build.MerkleRoot),
			pkgInfo.Tags,
		}

		blobs, err := loadBlobs(pkgInfo.BlobsPath)
		if err != nil {
			return nil, err
		}

		for _, blob := range blobs {
			pkg.Files[blob.Path] = blob.Merkle
			snapshot.Blobs[blob.Merkle] = build.BlobInfo{blob.Size}
		}

		snapshot.Packages[pkgInfo.Name] = pkg
	}

	if c.manifestPath != "" {
		index, err := parseManifest(c.manifestPath)
		if err != nil {
			return nil, err
		}

		for _, entry := range index {
			pkg := build.Package{
				make(map[string]build.MerkleRoot),
				entry.Tags,
			}

			blobs, err := loadBlobs(entry.BlobsPath)
			if err != nil {
				return nil, err
			}

			for _, blob := range blobs {
				pkg.Files[blob.Path] = blob.Merkle
				snapshot.Blobs[blob.Merkle] = build.BlobInfo{blob.Size}
			}

			snapshot.Packages[entry.Name] = pkg

		}
	}

	return snapshot, nil
}

// Run executes the snapshot command
func Run(cfg *build.Config, args []string) error {
	config, err := parseConfig(args)
	if err != nil {
		return err
	}

	snapshot, err := buildSnapshot(*config)
	if err != nil {
		return err
	}

	data, err := json.MarshalIndent(snapshot, "", "  ")
	if err != nil {
		return err
	}

	if err := ioutil.WriteFile(config.outputPath, data, 0644); err != nil {
		return err
	}

	return nil
}
