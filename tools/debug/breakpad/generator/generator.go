// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package generator

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"sync"

	breakpad "go.fuchsia.dev/fuchsia/tools/debug/breakpad/lib"
	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
)

// The default module name for modules that don't have a soname, e.g., executables and
// loadable modules. This allows us to use the same module name at runtime as sonames are
// the only names that are guaranteed to be available at build and run times. This value
// must be kept in sync with what Crashpad uses at run time for symbol resolution to work
// properly.
const defaultModuleName = "<_>"

// The module OS used to overwrite existing OS values in generated symbol files, even if
// they're already set to something else.
const replacementModuleOS = "Fuchsia"

// Generate generates breakpad symbol data for each of the input elflib.BinaryFileRefs.
// Returns the path to a directory containing the generated files, or the empty string if
// an error occurred.
func Generate(bfrs []elflib.BinaryFileRef, dumpSymsPath string) (path string, err error) {
	outc := make(chan string)
	errc := make(chan error)
	defer close(outc)
	defer close(errc)

	g := &generator{
		dumpSymsPath: dumpSymsPath,
		visited:      make(map[string]bool),
		visitedMutex: &sync.Mutex{},
	}

	jobs := make(chan elflib.BinaryFileRef)
	go g.run(jobs, outc, errc)
	for _, bfr := range bfrs {
		jobs <- bfr
	}
	close(jobs)

	select {
	case err = <-errc:
		return "", err
	case path = <-outc:
		return path, nil
	}
}

// Generator is a helper class for executing Breakpad's dump_syms tool.
//
// The run method is meant to be executed as a go-routine. It will manage its own working
// directory, and publish the path to that directory only on success.
//
// The run method is threadsafe, and will skip files that have already been processed.
type generator struct {
	// The path to the Breakpad dump_syms executable.
	dumpSymsPath string

	// Filepaths that have already been processed by this generator.
	visited      map[string]bool
	visitedMutex *sync.Mutex
}

// Run executes this generator on the given channel of elflib.BinarFileRefs.
//
// A temp directory is created to store generated files. On success, the directory is
// emitted on out. On the first encountered error, the generator will emit the error on
// errs, delete the output directory, and exit.
func (g *generator) run(in <-chan elflib.BinaryFileRef, out chan<- string, errs chan<- error) {
	outdir, err := ioutil.TempDir("", "breakpad")
	if err != nil {
		errs <- err
		return
	}
	if err := g.generate(in, outdir); err != nil {
		errs <- err
		os.RemoveAll(outdir)
		return
	}
	out <- outdir
}

func (g *generator) generate(in <-chan elflib.BinaryFileRef, outdir string) error {
	for bfr := range in {
		if !g.markVisited(bfr.Filepath) {
			continue
		}
		sf, err := g.genFromBinaryFileRef(bfr)
		if err != nil {
			return err
		}
		fd, err := os.Create(filepath.Join(outdir, fmt.Sprintf("%s.sym", bfr.BuildID)))
		if err != nil {
			return err
		}
		defer fd.Close()
		if _, err := sf.WriteTo(fd); err != nil {
			return err
		}
	}
	return nil
}

func (g *generator) genFromBinaryFileRef(bfr elflib.BinaryFileRef) (*breakpad.SymbolFile, error) {
	log.Printf("generating symbols for %q", bfr.Filepath)
	sf, err := g.genSymbolFile(bfr)
	if err != nil {
		return nil, err
	}
	sf.ModuleSection.OS = replacementModuleOS
	sf.ModuleSection.ModuleName = defaultModuleName
	soname, err := g.readSoName(bfr.Filepath)
	if err == nil && soname != "" {
		sf.ModuleSection.ModuleName = soname
	}
	return sf, nil
}

func (g *generator) genSymbolFile(bfr elflib.BinaryFileRef) (*breakpad.SymbolFile, error) {
	var stdout bytes.Buffer
	cmd := exec.Cmd{
		Path:   g.dumpSymsPath,
		Args:   []string{g.dumpSymsPath, bfr.Filepath},
		Stdout: &stdout,
	}
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("command failed %v: %w", cmd.Args, err)
	}
	return breakpad.ParseSymbolFile(&stdout)
}

func (g *generator) readSoName(path string) (string, error) {
	fd, err := os.Open(path)
	if err != nil {
		return "", fmt.Errorf("open failed %q: %w", path, err)
	}
	defer fd.Close()
	return elflib.GetSoName(path, fd)
}

// Marks that path has been visited and returs true iff this generator has not alread
// visited path. Otherwise returns false.
func (g *generator) markVisited(path string) (succeeded bool) {
	g.visitedMutex.Lock()
	defer g.visitedMutex.Unlock()
	if g.visited[path] {
		return false
	}
	g.visited[path] = true
	return true
}
