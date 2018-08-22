///bin/true ; exec /usr/bin/env go run "$0" "$@"
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"strings"
)

var output = flag.String("output", "", "Path to the generated tarball")
var manifest = flag.String("manifest", "", "Path to the file containing a description of the tarball's contents")

func writeToArchive(archive *tar.Writer, path string) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()
	if _, err = io.Copy(archive, file); err != nil {
		return err
	}
	return nil
}

func createTar(archive string, mappings map[string]string) error {
	file, err := os.Create(archive)
	if err != nil {
		return err
	}
	defer file.Close()

	gw := gzip.NewWriter(file)
	defer gw.Close()
	tw := tar.NewWriter(gw)
	defer tw.Close()

	for dest, src := range mappings {
		info, err := os.Stat(src)
		if err != nil {
			return err
		}

		header, err := tar.FileInfoHeader(info, info.Name())
		if err != nil {
			return err
		}
		header.Name = dest
		header.Uid = 0
		header.Gid = 0
		if err = tw.WriteHeader(header); err != nil {
			return err
		}

		// Using a separate function for this step to ensure copied files are closed
		// in a timely manner.
		if err = writeToArchive(tw, src); err != nil {
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
	if err = createTar(output, mappings); err != nil {
		return err
	}
	return nil
}

func main() {
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, `Usage: tar_maker --manifest path/to/manifest

This tool creates a tarball whose contents are described in a file manifest.
Each line in the manifest should be of the form:
    <path in tarball>=<path to source file>

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
