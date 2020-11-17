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
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"runtime/pprof"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjagraph"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
)

var (
	ninjalogPath = flag.String("ninjalog", "", "path of .ninja_log")
	compdbPath   = flag.String("compdb", "", "path of JSON compilation database")
	graphPath    = flag.String("graph", "", "path of graphviz dot file for ninja targets")
	criticalPath = flag.Bool("critical-path", false, "whether only include critical path in the trace, --graph must be set for this to work")
	buildDir     = flag.String("build-dir", "", "path of the directory where ninja is run; when non-empty, ninjatrace will look for subtraces in this directory to interleave in the main trace")
	granularity  = flag.Duration("granularity", 100*time.Millisecond, "time granularity used to filter short events in interleaved sub traces, for example traces from clang and rustc; this flag does NOT affect the main trace")

	traceJSON  = flag.String("trace-json", "trace.json", "output path of trace.json")
	cpuprofile = flag.String("cpuprofile", "", "file to write cpu profile")
)

type artifacts struct {
	steps    []ninjalog.Step
	commands []compdb.Command
	graph    ninjagraph.Graph
}

func join(as artifacts, criticalPath bool) ([]ninjalog.Step, error) {
	steps := as.steps
	if len(as.commands) > 0 {
		steps = ninjalog.Populate(steps, as.commands)
	}

	if criticalPath {
		if err := as.graph.PopulateEdges(steps); err != nil {
			return nil, err
		}
		var err error
		steps, err = as.graph.PopulatedSteps()
		if err != nil {
			return nil, err
		}
	}
	return steps, nil
}

func readArtifacts(logPath, compdbPath, graphPath string) (artifacts, error) {
	var ret artifacts

	f, err := os.Open(logPath)
	if err != nil {
		return artifacts{}, fmt.Errorf("opening ninjalog file: %v", err)
	}
	defer f.Close()
	njl, err := ninjalog.Parse(logPath, f)
	if err != nil {
		return artifacts{}, fmt.Errorf("parsing ninjalog: %v", err)
	}
	// TODO(jayzhuang): Dedup and Populate could be methods on NinjaLog.
	ret.steps = ninjalog.Dedup(njl.Steps)

	if compdbPath != "" {
		f, err := os.Open(compdbPath)
		if err != nil {
			return artifacts{}, fmt.Errorf("opening compdb file: %v", err)
		}
		defer f.Close()
		ret.commands, err = compdb.Parse(f)
		if err != nil {
			return artifacts{}, fmt.Errorf("parsing compdb: %v", err)
		}
	}

	if graphPath != "" {
		f, err := os.Open(graphPath)
		if err != nil {
			return artifacts{}, fmt.Errorf("opening ninja graph file: %v", err)
		}
		defer f.Close()
		ret.graph, err = ninjagraph.FromDOT(f)
		if err != nil {
			return artifacts{}, fmt.Errorf("parsing ninja graph: %v", err)
		}
	}

	return ret, nil
}

func createAndWriteTrace(path string, traces []ninjalog.Trace) (err error) {
	f, err := os.Create(*traceJSON)
	if err != nil {
		return fmt.Errorf("creating trace output file %q: %v", path, err)
	}
	defer func() {
		if cerr := f.Close(); cerr != nil && err == nil {
			err = fmt.Errorf("closing trace ouptut file: %q: %v", path, err)
		}
	}()

	if err := json.NewEncoder(f).Encode(traces); err != nil {
		return fmt.Errorf("writing trace: %v", err)
	}
	return nil
}

func main() {
	flag.Parse()

	if *ninjalogPath == "" {
		log.Fatalf("--ninjalog is required")
	}

	if *criticalPath {
		if *graphPath == "" {
			log.Fatalf("--graph must be set when --critical-path is true")
		}
	}

	if *cpuprofile != "" {
		f, err := os.Create(*cpuprofile)
		if err != nil {
			log.Fatalf("Failed to create CPU profile: %v", err)
		}
		defer f.Close()
		if err := pprof.StartCPUProfile(f); err != nil {
			log.Fatalf("Failed to start CPU profile: %v", err)
		}
		defer pprof.StopCPUProfile()
	}

	as, err := readArtifacts(*ninjalogPath, *compdbPath, *graphPath)
	if err != nil {
		log.Fatalf("Failed to read artifacts: %v", err)
	}

	steps, err := join(as, *criticalPath)
	if err != nil {
		log.Fatalf("Failed to join information from ninjalog, compdb and ninjagraph together: %v", err)
	}
	traces := ninjalog.ToTraces(ninjalog.Flow(steps), 1)

	if *buildDir != "" {
		interleaved, err := ninjalog.ClangTracesToInterleave(traces, *buildDir, *granularity)
		if err != nil {
			log.Fatalf("Failed to interleave clang trace: %v", err)
		}
		traces = append(traces, interleaved...)
	}

	if *traceJSON != "" {
		if err := createAndWriteTrace(*traceJSON, traces); err != nil {
			log.Fatalf("Failed to create and write trace: %v", err)
		}
	}
}
