// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

var output = flag.String("output", "", "Path to the generated tarball")
var manifest = flag.String("manifest", "", "Path to the file containing a description of the tarball's contents")

func archiveFile(tw *tar.Writer, src, dest string) error {
	file, err := os.Open(src)
	if err != nil {
		return err
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil {
		return err
	}

	hdr, err := tar.FileInfoHeader(info, info.Name())
	if err != nil {
		return err
	}
	hdr.Name = dest
	hdr.Uid = 0
	hdr.Gid = 0
	if err := tw.WriteHeader(hdr); err != nil {
		return err
	}
	_, err = io.Copy(tw, file)
	return err
}

func archiveDirectory(tw *tar.Writer, src, dest string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		fileDest := filepath.Join(dest, path[len(src)+1:])
		return archiveFile(tw, path, fileDest)
	})
}

func createTar(archive string, mappings map[string]string) error {
	file, err := os.Create(archive)
	if err != nil {
		return err
	}
	defer file.Close()

	w := NewWriter(file)
	defer w.Close()
	tw := tar.NewWriter(w)
	defer tw.Close()

	// Sort by destination path to ensure deterministic output.
	destPaths := make([]string, 0, len(mappings))
	for dst := range mappings {
		destPaths = append(destPaths, dst)
	}
	sort.Strings(destPaths)
	for _, dest := range destPaths {
		src := mappings[dest]
		info, err := os.Stat(src)
		if err != nil {
			return err
		}
		if info.IsDir() {
			err = archiveDirectory(tw, src, dest)
		} else {
			err = archiveFile(tw, src, dest)
		}
		if err != nil {
			return err
		}
	}

	return nil
}

func readManifest(manifest string) (map[string]string, error) {
	file, err := os.Open(manifest)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	result := make(map[string]string)

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		parts := strings.SplitN(scanner.Text(), "=", 2)
		result[parts[0]] = parts[1]
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return result, nil
}

func generateArchive(manifest string, output string) error {
	if _, err := os.Stat(output); err == nil {
		// The file exists, need to remove it first.
		if err := os.Remove(output); err != nil {
			return err
		}
	}
	mappings, err := readManifest(manifest)
	if err != nil {
		return err
	}
	return createTar(output, mappings)
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `Usage: tar_maker --manifest path/to/manifest

This tool creates a tarball whose contents are described in a file manifest.
Each line in the manifest should be of the form:
    <path in tarball>=<path to source file or dir>

The tarball is compressed using gzip.
`)
		flag.PrintDefaults()
	}
	flag.Parse()

	if *output == "" {
		flag.Usage()
		log.Fatalf("Error: missing -output flag.")
	}
	if *manifest == "" {
		flag.Usage()
		log.Fatalf("Error: missing -manifest flag.")
	}

	if err := generateArchive(*manifest, *output); err != nil {
		log.Fatalf("Error: unable to create archive %s: %v", *output, err)
	}
}
