// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ninjatrace converts .ninja_log into trace-viewer formats.
//
// usage:
//  $ go run ninjatrace.go --filename out/debug-x64/.ninja_log
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

	"fuchsia.googlesource.com/tools/ninjalog"
)

var (
	filename   = flag.String("filename", ".ninja_log", "filename of .ninja_log")
	traceJSON  = flag.String("trace_json", "trace.json", "output filename of trace.json")
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
	steps := ninjalog.Dedup(njl.Steps)
	flow := ninjalog.Flow(steps)
	return ninjalog.ToTraces(flow, 1), nil
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

	traces, err := convert(*filename)
	if err != nil {
		log.Fatal(err)
	}
	if *traceJSON != "" {
		err = output(*traceJSON, traces)
		if err != nil {
			log.Fatal(err)
		}
	}
}
