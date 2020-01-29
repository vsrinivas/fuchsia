// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"archive/tar"
	"compress/bzip2"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"time"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/runner"
)

var (
	debugArchive   string
	buildIDDir     string
	outputManifest string
	cpu            string
	osName         string
	dumpSyms       string
	colors         color.EnableColor
	level          logger.LogLevel
	timeout        time.Duration
	tasks          int
)

func init() {
	colors = color.ColorAuto
	level = logger.WarningLevel

	flag.StringVar(&debugArchive, "debug-archive", "", "path to archive of debug binaries")
	flag.StringVar(&buildIDDir, "build-id-dir", "", "path to .build-id directory to add debug binaries to")
	flag.StringVar(&outputManifest, "output-manifest", "", "path to output a json manifest of debug binaries to")
	flag.StringVar(&cpu, "cpu", "", "the architecture of the binaries in the archive")
	flag.StringVar(&osName, "os", "", "the os of the binaries in the archive")
	flag.StringVar(&dumpSyms, "dump-syms", "", "the path to the dump_syms tool")
	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
	flag.DurationVar(&timeout, "timeout", 0, "the amount of time to wait on all dump_syms tasks")
	// By default we set the number of tasks to 2 times the number of CPUs. The expectation is that its
	// good to have many more tasks than CPUs because the tasks are IO bound.
	flag.IntVar(&tasks, "j", 2*runtime.NumCPU(), "the number of concurrent tasks to run at once")
}

type binary struct {
	CPU      string `json:"cpu"`
	Debug    string `json:"debug"`
	BuildID  string `json:"elf_build_id"`
	OS       string `json:"os"`
	Breakpad string `json:"breakpad"`
}

type binaryRef struct {
	ref      elflib.BinaryFileRef
	breakpad string
}

// createFile creates a write only file and all required directories.
func createFile(file string, flags ...int) (*os.File, error) {
	if err := os.MkdirAll(filepath.Dir(file), os.ModePerm); err != nil {
		return nil, err
	}
	flagSet := os.O_WRONLY | os.O_CREATE
	for _, flag := range flags {
		flagSet = flagSet | flag
	}
	return os.OpenFile(file, flagSet, 0755)
}

var buildIDFileRE = regexp.MustCompile("^([0-9a-f][0-9a-f])/([0-9a-f]+).debug$")

func archiveExists(debugArchive string) (bool, error) {
	info, err := os.Stat(debugArchive)
	if err != nil {
		if os.IsNotExist(err) {
			return false, nil
		}
		return true, err
	}
	return !info.IsDir(), nil
}

func isBuildIDDir(ctx context.Context, dir string, contents []os.FileInfo) bool {
	for _, info := range contents {
		// Some special files are allowed and expected.
		if info.Name() == "LICENSE" {
			continue
		}
		logger.Tracef(ctx, "checking %s", info.Name())
		// Outside of particular special files, everything should be a directory.
		if !info.IsDir() {
			logger.Tracef(ctx, "%s wasn't a directory", info.Name())
			return false
		}
		// The directory name should be a hex byte.
		if len(info.Name()) != 2 {
			return false
		}
		if _, err := strconv.ParseUint(info.Name(), 16, 8); err != nil {
			logger.Tracef(ctx, "%s wasn't valid hex digit: %v", info.Name(), err)
			return false
		}
		// Now that we know the directory name was a 2 digit hex value, it's safe
		// to assume its most likely a .build-id directory and we can check to see
		// if it contains the sorts of files we expect
		files, err := ioutil.ReadDir(filepath.Join(dir, info.Name()))
		if err != nil {
			logger.Tracef(ctx, "%s couldn't read directory: %v", info.Name(), err)
			return false
		}
		logger.Tracef(ctx, "checking %s/... to see if it has the right files", info.Name())
		for _, file := range files {
			path := filepath.Join(info.Name(), file.Name())
			logger.Tracef(ctx, "checking %s for validity", path)
			// We should have exclusivelly files here.
			if file.IsDir() {
				logger.Tracef(ctx, "%s was a directory, not a file", path)
				return false
			}
			// For the time being everything should match this regex. This regex can be extended for
			// other extensions as well, even arbitrary extensions. Right now its just .debug.
			if !buildIDFileRE.MatchString(path) {
				logger.Tracef(ctx, "%s didn't match", path)
				return false
			}
		}
	}
	return true
}

// getStartDir allows us to be flexible in what we accept for debugArchive.
// This lets us use both a directory and a file for the time being allowing
// for an easy soft transistion as needed.
func getStartDir() string {
	info, err := os.Stat(debugArchive)
	if err != nil {
		return filepath.Dir(debugArchive)
	}
	if info.IsDir() {
		return debugArchive
	} else {
		return filepath.Dir(debugArchive)
	}
}

// findBuildIDDir allows us to find a build id directory associated with the
// debugArchive location. When transitioning from a .tar.bz2 file to a .build-id
// directory the debugArchive will remain the same and this allows the code
// to still work.
func findBuildIDDir(ctx context.Context, startDir string) (string, error) {
	if startDir == "" {
		return "", fmt.Errorf("Could not find a .build-id directory")
	}
	logger.Tracef(ctx, "checking %s", startDir)
	infos, err := ioutil.ReadDir(startDir)
	if err != nil {
		return "", err
	}
	logger.Tracef(ctx, "inspecting %s", startDir)
	if isBuildIDDir(ctx, startDir, infos) {
		return startDir, nil
	}
	return findBuildIDDir(ctx, filepath.Dir(startDir))
}

// runDumpSyms starts a dump_syms command using `br` that converts an ELF file
// with debug info into a breakpad syms file. If the breakpad syms file has already
// been created, dump_syms will not run. This allows many instances of this tool to
// run at the same time without duplicating work. Note that creating a file with
// O_EXCL is atomic.
func runDumpSyms(ctx context.Context, br *runner.BatchRunner, in, out string) error {
	file, err := createFile(out, os.O_EXCL)
	// If the file already exists, don't bother recreating it. This saves a massive
	// amount of computation and is atomic.
	if os.IsExist(err) {
		logger.Tracef(ctx, "file %s already existed: %v", out, err)
		return nil
	} else if err != nil {
		return err
	}
	cmd := []string{dumpSyms, "-r", "-n", "<_>", "-o", "Fuchsia", in}
	br.Enqueue(cmd, file, os.Stderr, func() {
		file.Close()
	})
	return nil
}

// produceSymbols takes an input .build-id directory and produces breakpad symbols
// for each binary in the input .build-id directory.
func produceSymbols(ctx context.Context, inputBuildIDDir string, br *runner.BatchRunner) ([]binaryRef, error) {
	logger.Tracef(ctx, "about to walk build-id-dir")
	refs, err := elflib.WalkBuildIDDir(inputBuildIDDir)
	if err != nil {
		return nil, fmt.Errorf("while calling dump_syms for %s: %v", inputBuildIDDir, err)
	}
	logger.Tracef(ctx, "about spin up dump_syms")
	outs := []binaryRef{}
	// alot 3 seconds per binary to run dump_syms
	for _, ref := range refs {
		out := filepath.Join(buildIDDir, ref.BuildID[:2], ref.BuildID[2:]+".sym")
		logger.Tracef(ctx, "spun up dump_syms for %s", out)
		if err := runDumpSyms(ctx, br, ref.Filepath, out); err != nil {
			return nil, fmt.Errorf("while calling dump_syms for %s: %v", err)
		}
		outs = append(outs, binaryRef{ref, out})
	}
	return outs, nil
}

// unpack takes debugArchive and unpacks each debug binary. On seeing a debug binary
// dump_syms is invoked for it using `br` as well.
func unpack(ctx context.Context, br *runner.BatchRunner) ([]binaryRef, error) {
	// unpack each debug binary into buildIDDir
	file, err := os.Open(debugArchive)
	if err != nil {
		return nil, fmt.Errorf("while unpacking %s: %v", debugArchive, err)
	}
	defer file.Close()
	// The file is bzip2 compressed
	tr := tar.NewReader(bzip2.NewReader(file))
	out := []binaryRef{}
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
			logger.Warningf(ctx, "%s in %s was not a debug binary", hdr.Name, debugArchive)
			continue
		}
		logger.Tracef(ctx, "Reading %s from %s", hdr.Name, debugArchive)
		if len(matches) != 3 {
			panic("The list of matches isn't as expected")
		}
		buildID := matches[1] + matches[2]
		unpackFilePath := filepath.Join(buildIDDir, hdr.Name)
		outFile, err := createFile(unpackFilePath)
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
		symbolFile := filepath.Join(buildIDDir, bfr.BuildID[:2], bfr.BuildID[2:]+".sym")
		logger.Tracef(ctx, "adding dump_syms command for %s -> %s", bfr.Filepath, symbolFile)
		if err := runDumpSyms(ctx, br, bfr.Filepath, symbolFile); err != nil {
			return nil, fmt.Errorf("running dumpSyms on %s to produce %s: %v", bfr.Filepath, symbolFile, err)
		}
		out = append(out, binaryRef{bfr, symbolFile})
	}
	return out, nil
}

func writeManifest(bfrs []binaryRef) error {
	out := []binary{}
	for _, bfr := range bfrs {
		out = append(out, binary{
			CPU:      cpu,
			Debug:    bfr.ref.Filepath,
			BuildID:  bfr.ref.BuildID,
			Breakpad: bfr.breakpad,
			OS:       osName,
		})
	}
	file, err := createFile(outputManifest)
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
	flag.Parse()
	log := logger.NewLogger(level, color.NewColor(colors), os.Stderr, os.Stderr, "")
	ctx, cancel := context.WithCancel(context.Background())
	ctx = logger.WithLogger(ctx, log)
	if debugArchive == "" {
		log.Fatalf("-debug-archive is required.")
	}
	if buildIDDir == "" {
		log.Fatalf("-build-id-dir is required.")
	}
	if outputManifest == "" {
		log.Fatalf("-output-manifest is required.")
	}

	br := runner.NewBatchRunner(ctx, &runner.SubprocessRunner{}, tasks)

	exists, err := archiveExists(debugArchive)
	if err != nil {
		log.Fatalf("while checking if archive existed: %v", err)
	}

	bfrs := []binaryRef{}

	log.Tracef("checking!")
	if exists {
		log.Tracef("archive existed")
		bfrs, err = unpack(ctx, br)
		if err != nil {
			log.Fatalf("%v", err)
		}
	} else {
		startDir := getStartDir()
		log.Tracef("found %s as start directory", startDir)
		dir, err := findBuildIDDir(ctx, startDir)
		if err != nil {
			log.Fatalf("while finding .build-id directory: %v", err)
		}
		bfrs, err = produceSymbols(ctx, dir, br)
	}

	// TODO: write the manifest to a tmp file and rename it into place.
	log.Tracef("writing manifest now")
	if err = writeManifest(bfrs); err != nil {
		log.Fatalf("%v", err)
	}

	log.Tracef("manifest written")

	// Before we wait on all the dump_syms calls we need to ensure that we time out eventully
	log.Tracef("waiting on all dump_syms calls to finish")
	if timeout != 0 {
		time.AfterFunc(timeout, cancel)
	}
	if err := br.Wait(); err != nil {
		log.Fatalf("while waiting on dump_syms to finish: %v", err)
	}
	log.Tracef("finished")
}
