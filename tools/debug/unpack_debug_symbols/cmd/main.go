// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"compress/bzip2"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

var (
	debugArchive   string
	buildIDDir     string
	outputManifest string
	cpu            string
	osName         string
	colors         color.EnableColor
	level          logger.LogLevel
)

func init() {
	colors = color.ColorAuto
	level = logger.WarningLevel

	flag.StringVar(&debugArchive, "debug-archive", "", "path to archive of debug binaries")
	flag.StringVar(&buildIDDir, "build-id-dir", "", "path to .build-id directory to add debug binaries to")
	flag.StringVar(&outputManifest, "output-manifest", "", "path to output a json manifest of debug binaries to")
	flag.StringVar(&cpu, "cpu", "", "the architecture of the binaries in the archive")
	flag.StringVar(&osName, "os", "", "the os of the binaries in the archive")
	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
}

type binary struct {
	CPU     string `json:"cpu"`
	Debug   string `json:"debug"`
	BuildID string `json:"elf_build_id"`
	OS      string `json:"os"`
}

var buildIDFileRE = regexp.MustCompile("^([0-9a-f][0-9a-f])/([0-9a-f]+).debug$")

func unpack(log *logger.Logger) ([]elflib.BinaryFileRef, error) {
	// unpack each debug binary into buildIDDir
	file, err := os.Open(debugArchive)
	if err != nil {
		return nil, fmt.Errorf("while unpacking %s: %v", debugArchive, err)
	}
	defer file.Close()
	// The file is bzip2 compressed
	tr := tar.NewReader(bzip2.NewReader(file))
	out := []elflib.BinaryFileRef{}
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("while reading %s: %v", debugArchive, err)
		}
		matches := buildIDFileRE.FindStringSubmatch(hdr.Name)
		if matches == nil {
			log.Warningf("%s in %s was not a debug binary", hdr.Name, debugArchive)
			continue
		}
		log.Tracef("Reading %s from %s", hdr.Name, debugArchive)
		if len(matches) != 3 {
			panic("The list of matches isn't as expected")
		}
		buildID := matches[1] + matches[2]
		unpackFilePath := filepath.Join(buildIDDir, hdr.Name)
		if err = os.MkdirAll(filepath.Dir(unpackFilePath), os.ModePerm); err != nil {
			return nil, fmt.Errorf("while attempting to write %s from %s to %s: %v", hdr.Name, debugArchive, unpackFilePath, err)
		}
		outFile, err := os.Create(unpackFilePath)
		if err != nil {
			return nil, fmt.Errorf("while attempting to write %s from %s to %s: %v", hdr.Name, debugArchive, unpackFilePath, err)
		}
		if _, err := io.Copy(outFile, tr); err != nil {
			outFile.Close()
			return nil, fmt.Errorf("while attempting to write %s from %s to %s: %v", hdr.Name, debugArchive, unpackFilePath, err)
		}
		outFile.Close()
		bfr := elflib.NewBinaryFileRef(unpackFilePath, buildID)
		if err := bfr.Verify(); err != nil {
			return nil, fmt.Errorf("while attempting to verify %s copied from %s: %v", unpackFilePath, debugArchive, err)
		}
		out = append(out, bfr)
	}
	return out, nil
}

func writeManifest(bfrs []elflib.BinaryFileRef) error {
	out := []binary{}
	for _, bfr := range bfrs {
		out = append(out, binary{
			CPU:     cpu,
			Debug:   bfr.Filepath,
			BuildID: bfr.BuildID,
			OS:      osName,
		})
	}
	file, err := os.Create(outputManifest)
	if err != nil {
		return fmt.Errorf("while writing json to %s: %v", outputManifest, err)
	}
	defer file.Close()
	if err = json.NewEncoder(file).Encode(out); err != nil {
		return fmt.Errorf("while writing json to %s: %v", outputManifest, err)
	}
	return nil
}

func main() {
	log := logger.NewLogger(level, color.NewColor(colors), os.Stdout, os.Stderr, "")
	flag.Parse()
	if debugArchive == "" {
		log.Fatalf("-debug-archive is required.")
	}
	if buildIDDir == "" {
		log.Fatalf("-build-id-dir is required.")
	}
	if outputManifest == "" {
		log.Fatalf("-output-manifest is required.")
	}

	bfrs, err := unpack(log)
	if err != nil {
		log.Fatalf("%v", err)
	}
	if err = writeManifest(bfrs); err != nil {
		log.Fatalf("%v", err)
	}
}
