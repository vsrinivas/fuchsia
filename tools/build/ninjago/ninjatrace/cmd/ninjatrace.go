// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ninjatrace converts .ninja_log into trace-viewer formats.
//
// usage:
//  $ go run ninjatrace.go --ninjalog out/debug-x64/.ninja_log
//
package main

import (
	"bufio"
	"compress/gzip"
	"encoding/json"
	"flag"
	"io"
	"log"
	"os"
	"path/filepath"
	"runtime/pprof"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjagraph"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
)

var (
	ninjalogPath = flag.String("ninjalog", "", "path of .ninja_log")
	compdbPath   = flag.String("compdb", "", "path of JSON compilation database")
	graphPath    = flag.String("graph", "", "path of graphviz dot file for ninja targets")
	criticalPath = flag.Bool("critical-path", false, "whether only include critical path in the trace, --ninjagraph must be set for this to work")

	traceJSON  = flag.String("trace-json", "trace.json", "output path of trace.json")
	cpuprofile = flag.String("cpuprofile", "", "file to write cpu profile")
)

func reader(fname string, rd io.Reader) (io.Reader, error) {
	if filepath.Ext(fname) != ".gz" {
		return bufio.NewReaderSize(rd, 512*1024), nil
	}
	return gzip.NewReader(bufio.NewReaderSize(rd, 512*1024))
}

func convert(fname string) ([]ninjalog.Trace, error) {
	f, err := os.Open(fname)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	rd, err := reader(fname, f)
	if err != nil {
		return nil, err
	}

	njl, err := ninjalog.Parse(fname, rd)
	if err != nil {
		return nil, err
	}
	// TODO: Dedup and Populate could be methods on NinjaLog.
	steps := ninjalog.Dedup(njl.Steps)
	if *compdbPath != "" {
		f, err := os.Open(*compdbPath)
		if err != nil {
			return nil, err
		}
		defer f.Close()
		commands, err := compdb.Parse(f)
		if err != nil {
			return nil, err
		}
		steps = ninjalog.Populate(steps, commands)
	}

	if *criticalPath {
		dotFile, err := os.Open(*graphPath)
		if err != nil {
			return nil, err
		}
		defer dotFile.Close()
		graph, err := ninjagraph.FromDOT(dotFile)
		if err != nil {
			return nil, err
		}
		if err := graph.PopulateEdges(steps); err != nil {
			return nil, err
		}
		steps, err = graph.CriticalPath()
		if err != nil {
			return nil, err
		}
	}
	return ninjalog.ToTraces(ninjalog.Flow(steps), 1), nil
}

func output(fname string, traces []ninjalog.Trace) (err error) {
	f, err := os.Create(fname)
	if err != nil {
		return err
	}
	defer func() {
		cerr := f.Close()
		if err == nil {
			err = cerr
		}
	}()
	js, err := json.Marshal(traces)
	if err != nil {
		return err
	}
	_, err = f.Write(js)
	return err
}

func main() {
	flag.Parse()

	if *cpuprofile != "" {
		f, err := os.Create(*cpuprofile)
		if err != nil {
			log.Fatal(err)
		}
		pprof.StartCPUProfile(f)
		defer pprof.StopCPUProfile()
	}

	traces, err := convert(*ninjalogPath)
	if err != nil {
		log.Fatal(err)
	}
	if *traceJSON != "" {
		if err := output(*traceJSON, traces); err != nil {
			log.Fatal(err)
		}
	}
}
