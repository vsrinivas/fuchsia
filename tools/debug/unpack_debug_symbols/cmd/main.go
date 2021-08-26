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
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

var (
	debugArchive   string
	buildIDDirIn   string
	buildIDDirOut  string
	outputManifest string
	cpu            string
	osName         string
	depfile        string
	dumpSyms       string
	colors         color.EnableColor
	level          logger.LogLevel
	timeout        time.Duration
	tasks          int
)

var (
	errNoBuildIDDir = fmt.Errorf("Could not find a .build-id directory")
)

func init() {
	colors = color.ColorAuto
	level = logger.WarningLevel

	flag.StringVar(&debugArchive, "debug-archive", "", "path to archive of debug binaries")
	flag.StringVar(&buildIDDirIn, "build-id-dir-in", "", "path to an input .build-id directory")
	flag.StringVar(&buildIDDirOut, "build-id-dir-out", "", "path to an output .build-id directory to populate")
	flag.StringVar(&outputManifest, "output-manifest", "", "path to output a json manifest of debug binaries to")
	flag.StringVar(&cpu, "cpu", "", "the architecture of the binaries in the archive")
	flag.StringVar(&osName, "os", "", "the os of the binaries in the archive")
	flag.StringVar(&depfile, "depfile", "", "the depfile to emit")
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

// We don't want extensions to be matched when we look for substring matches, so
// convenient just to drop it from consideration when validating the directory
// layout.
var buildIDFileNoExtRE = regexp.MustCompile("^([0-9a-f][0-9a-f])/([0-9a-f]+)$")

func isBuildIDDir(ctx context.Context, dir string, contents []os.FileInfo) bool {
	for _, info := range contents {
		// Some special files are allowed and expected.
		if info.Name() == "LICENSE" || info.Name()[0] == '.' {
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
			// We should exclusively have files here.
			if file.IsDir() {
				logger.Tracef(ctx, "%s was a directory, not a file", path)
				return false
			}
			if !buildIDFileNoExtRE.MatchString(trimExt(path)) {
				logger.Tracef(ctx, "%s didn't match", path)
				return false
			}
		}
	}
	return true
}

func trimExt(p string) string {
	return strings.TrimSuffix(p, filepath.Ext(p))
}

// getStartDir allows us to be flexible in what we accept for debugArchive.
// This lets us use both a directory and a file for the time being allowing
// for an easy soft transistion as needed.
func getStartDir() (string, error) {
	if debugArchive == "" {
		return buildIDDirIn, nil
	}
	// If the debug archive was passed, just return the first ancestor
	// directory that exists. This allows flexibility in transitioning away
	// from specifying the debug archive altogether. The common two cases are
	// (1) the debug archive still exists and we will unpack the contents in
	// the parent directory, or
	// (2) the debug archive and some unnecessary nesting no longer exist, and
	// some ancestor directory is where the unpacked contents now await
	// processing.
	dir := filepath.Dir(debugArchive)
	for dir != "" && dir != "/" && dir != "." {
		info, err := os.Stat(dir)
		if err == nil {
			if !info.IsDir() {
				return "", fmt.Errorf("ancestor directory %s of %s is actually a file", dir, debugArchive)
			}
			return dir, nil
		} else if !os.IsNotExist(err) {
			return "", err
		}
		dir = filepath.Dir(dir)
	}
	return "", fmt.Errorf("no ancestor directory of %s exists", debugArchive)
}

// In the case where a debug archive is specified but is nonexistent, as will
// happen during the transition away from specifying the parameter altogether,
// this means that a raw .build-id dir is now being supplied. To allow
// flexibility around the relative path between the debug archive specification
// and this directory, we telescoped out in getStartDir to degugArchive's
// first existent ancestor directory; here we now telescope back in and look for
// the first .build-id subdirectory.
func findBuildIDDir(ctx context.Context, startDir string) (string, error) {
	logger.Tracef(ctx, "determining if %s is a .build-id directory", startDir)
	infos, err := ioutil.ReadDir(startDir)
	if err != nil {
		return "", err
	}
	if isBuildIDDir(ctx, startDir, infos) {
		return startDir, nil
	}
	for _, info := range infos {
		if !info.IsDir() {
			continue
		}
		dir, err := findBuildIDDir(ctx, filepath.Join(startDir, info.Name()))
		if err == nil {
			return dir, nil
		} else if err != errNoBuildIDDir {
			return "", err
		}
	}
	return "", errNoBuildIDDir
}

// runDumpSyms starts a dump_syms command using `br` that converts an ELF file
// with debug info into a breakpad syms file. If the breakpad syms file has already
// been created, dump_syms will not run. This allows many instances of this tool to
// run at the same time without duplicating work. Note that creating a file with
// O_EXCL is atomic.
func runDumpSyms(ctx context.Context, br *BatchRunner, in, out string) error {
	file, err := osmisc.CreateFile(out, os.O_WRONLY|os.O_EXCL)
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
// for each binary in the output .build-id directory.
func produceSymbols(ctx context.Context, inputBuildIDDir string, br *BatchRunner) ([]binaryRef, error) {
	logger.Tracef(ctx, "about to walk build-id-dir")
	refs, err := elflib.WalkBuildIDDir(inputBuildIDDir)
	if err != nil {
		return nil, fmt.Errorf("while calling dump_syms for %s: %w", inputBuildIDDir, err)
	}

	logger.Tracef(ctx, "about spin up dump_syms")
	outs := []binaryRef{}
	for _, ref := range refs {
		out := filepath.Join(buildIDDirOut, ref.BuildID[:2], ref.BuildID[2:]+".sym")
		logger.Tracef(ctx, "spun up dump_syms for %s", out)
		if err := runDumpSyms(ctx, br, ref.Filepath, out); err != nil {
			return nil, fmt.Errorf("while calling dump_syms for %s: %v", ref.Filepath, err)
		}
		outs = append(outs, binaryRef{ref, out})
	}
	return outs, nil
}

// unpack takes debugArchive and unpacks each debug binary. On seeing a debug binary
// dump_syms is invoked for it using `br` as well.
func unpack(ctx context.Context, br *BatchRunner) ([]binaryRef, error) {
	// unpack each debug binary into buildIDDirOut
	file, err := os.Open(debugArchive)
	if err != nil {
		return nil, fmt.Errorf("while unpacking %s: %w", debugArchive, err)
	}
	defer file.Close()
	// The file is bzip2 compressed
	tr := tar.NewReader(bzip2.NewReader(file))
	out := []binaryRef{}
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		} else if err != nil {
			return nil, fmt.Errorf("while reading %s: %w", debugArchive, err)
		} else if hdr.Typeflag == tar.TypeDir {
			continue
		}

		matches := buildIDFileNoExtRE.FindStringSubmatch(trimExt(hdr.Name))
		if matches == nil {
			logger.Warningf(ctx, "%s in %s was not a debug binary", hdr.Name, debugArchive)
			continue
		}
		logger.Tracef(ctx, "Reading %s from %s", hdr.Name, debugArchive)
		if len(matches) != 3 {
			panic("The list of matches isn't as expected")
		}
		buildID := matches[1] + matches[2]
		unpackFilePath := filepath.Join(buildIDDirOut, hdr.Name)

		ok, err := osmisc.FileExists(unpackFilePath)
		if err != nil {
			return nil, err
		} else if ok {
			continue
		}
		outFile, err := osmisc.CreateFile(unpackFilePath, os.O_WRONLY)
		if err != nil {
			return nil, fmt.Errorf("while attempting to write %s from %s to %s: %w", hdr.Name, debugArchive, unpackFilePath, err)
		}
		if _, err := io.Copy(outFile, tr); err != nil {
			outFile.Close()
			return nil, fmt.Errorf("while attempting to write %s from %s to %s: %w", hdr.Name, debugArchive, unpackFilePath, err)
		}
		outFile.Close()
		bfr := elflib.NewBinaryFileRef(unpackFilePath, buildID)
		if err := bfr.Verify(); err != nil {
			return nil, fmt.Errorf("while attempting to verify %s copied from %s: %w", unpackFilePath, debugArchive, err)
		}
		symbolFile := filepath.Join(buildIDDirOut, bfr.BuildID[:2], bfr.BuildID[2:]+".sym")
		logger.Tracef(ctx, "adding dump_syms command for %s -> %s", bfr.Filepath, symbolFile)
		if err := runDumpSyms(ctx, br, bfr.Filepath, symbolFile); err != nil {
			return nil, fmt.Errorf("running dumpSyms on %s to produce %s: %w", bfr.Filepath, symbolFile, err)
		}
		out = append(out, binaryRef{bfr, symbolFile})
	}
	return out, nil
}

// relIfAbs returns path relative to `base` if input `path` is absolute.
func relIfAbs(base, path string) (string, error) {
	if !filepath.IsAbs(path) {
		return path, nil
	}
	return filepath.Rel(base, path)
}

func writeManifest(bfrs []binaryRef, buildDir string) error {
	var out []binary
	for _, bfr := range bfrs {
		// Even though these files live outside of the build directory,
		// relativize them as is conventional for build API metadata.
		relDebug, err := relIfAbs(buildDir, bfr.ref.Filepath)
		if err != nil {
			return err
		}
		relBreakpad, err := relIfAbs(buildDir, bfr.breakpad)
		if err != nil {
			return err
		}

		out = append(out, binary{
			CPU:      cpu,
			Debug:    relDebug,
			BuildID:  bfr.ref.BuildID,
			Breakpad: relBreakpad,
			OS:       osName,
		})
	}
	file, err := osmisc.CreateFile(outputManifest, os.O_WRONLY)
	if err != nil {
		return fmt.Errorf("while writing json to %s: %w", outputManifest, err)
	}
	defer file.Close()
	enc := json.NewEncoder(file)
	enc.SetIndent("", "  ")
	if err = enc.Encode(out); err != nil {
		return fmt.Errorf("while writing json to %s: %w", outputManifest, err)
	}
	return nil
}

func main() {
	flag.Parse()
	log := logger.NewLogger(level, color.NewColor(colors), os.Stderr, os.Stderr, "")

	ctx, cancel := context.WithCancel(context.Background())
	ctx = logger.WithLogger(ctx, log)
	if buildIDDirOut == "" {
		log.Fatalf("-build-id-dir-out is required.")
	}
	if buildIDDirIn == "" && debugArchive == "" {
		log.Fatalf("one of -build-id-dir-in or -debug-archive is required.")
	}
	if outputManifest == "" {
		log.Fatalf("-output-manifest is required.")
	}
	if depfile == "" {
		log.Fatalf("-depfile is required.")
	}

	// This tool is always executed by the build in the build directory.
	buildDir, err := os.Getwd()
	if err != nil {
		log.Fatalf("Failed to get current working directory: %v", err)
	}

	// This action should rerun if the input .build-id directory or debug archive changes.
	var dep string
	if buildIDDirIn != "" {
		empty, err := osmisc.DirIsEmpty(buildIDDirIn)
		if err != nil {
			log.Fatalf("while checking if build-id-dir-in existed: %v", err)
		}
		if empty {
			if err := writeManifest(nil, buildDir); err != nil {
				log.Fatalf("failed to write empty manifest: %v", err)
			}
			log.Tracef("%s does not exist, no work needed", buildIDDirIn)
			return
		}
		dep = buildIDDirIn
	} else {
		dep = debugArchive
	}
	relDep, err := filepath.Rel(buildDir, dep)
	if err != nil {
		log.Fatalf("failed to relativize %s: %v", dep, err)
	}
	depfileContents := fmt.Sprintf("%s: %s", outputManifest, relDep)
	if err := ioutil.WriteFile(depfile, []byte(depfileContents), os.ModePerm); err != nil {
		log.Fatalf("failed to write depfile: %v", err)
	}

	// If the input .build-id directory is empty and no debug archive is
	// provided, then there is no real work to do: bail.
	if debugArchive == "" {
		empty, err := osmisc.DirIsEmpty(buildIDDirIn)
		if err != nil {
			log.Fatalf("error in checking state of build-id dir: %v", err)
		}
		if empty {
			if err := writeManifest(nil, buildDir); err != nil {
				log.Fatalf("failed to write empty manifest: %v", err)
			}
			log.Infof("build-id directory is empty; no work to do")
			return
		}
	}

	br := newBatchRunner(ctx, &subprocess.Runner{}, tasks)

	exists, err := osmisc.FileExists(debugArchive)
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
		startDir, err := getStartDir()
		if err != nil {
			log.Fatalf("could not find the start dir: %v", err)
		}
		log.Tracef("found %s as start directory", startDir)
		dir, err := findBuildIDDir(ctx, startDir)
		if err != nil {
			log.Fatalf("while finding .build-id directory: %v", err)
		}
		bfrs, err = produceSymbols(ctx, dir, br)
	}

	// TODO: write the manifest to a tmp file and rename it into place.
	log.Tracef("writing manifest now")
	if err = writeManifest(bfrs, buildDir); err != nil {
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
