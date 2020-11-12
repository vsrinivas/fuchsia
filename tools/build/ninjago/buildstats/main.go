// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.package main

// ninja_buildstats is an utility for extracting useful stats out of build
// artifacts from Ninja.
//
// It combines information ninjalog, compdb and ninja graph, extracts and
// serializes build stats from them.
//
// usage:
//  $ buildstats \
//      --ninjalog out/default/.ninja_log
//      --compdb path/to/compdb.json
//      --graph path/to/graph.dot
//      --output path/to/output.json
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"sort"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjagraph"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

var (
	ninjalogPath = flag.String("ninjalog", "", "path of .ninja_log")
	compdbPath   = flag.String("compdb", "", "path of JSON compilation database")
	graphPath    = flag.String("graph", "", "path of graphviz dot file for ninja targets")
	outputPath   = flag.String("output", "", "path to output the serialized build stats")

	colors color.EnableColor
	level  logger.LogLevel
)

func init() {
	colors = color.ColorAuto
	level = logger.ErrorLevel
	flag.Var(&colors, "color", "use color in output, can be never, auto, always")
	flag.Var(&level, "level", "output verbosity, can be fatal, error, warning, info, debug or trace")
}

type inputs struct {
	ninjalog, compdb, graph io.Reader
}

// action describes a build action.
//
// All fields are exported so this struct can be serialized by json.
type action struct {
	Command    string
	Outputs    []string
	Start, End time.Duration
	Rule       string
	Category   string
	// TotalFloat is the amount of time this step can be delayed without affecting
	// the completion time of the build.
	//
	// https://en.wikipedia.org/wiki/Float_(project_management)
	TotalFloat time.Duration
	// Drag is the amount of time this step is adding to the total build time. All
	// non-critial steps have zero drag.
	//
	// https://en.wikipedia.org/wiki/Critical_path_drag
	Drag time.Duration
}

// All fields are exported so this struct can be serialized by json.
type catBuildTime struct {
	// Name of this category.
	Category string
	// Number of actions in this category.
	Count int32
	// The sum of build times spent for all actions in this category.
	BuildTime time.Duration
	// Build time of the fastest and slowest action in this category.
	MinBuildTime, MaxBuildTime time.Duration
}

// All fields are exported so this struct can be serialized by json.
type buildStats struct {
	// CriticalPath is the build path that takes the longest time to finish.
	CriticalPath []action
	// Slowests includes the slowest 30 builds actions.
	Slowests []action
	// CatBuildTimes groups build times by category.
	CatBuildTimes []catBuildTime
	// Sum of build times of all actions.
	TotalBuildTime time.Duration
	// Wall time spent to complete this build.
	BuildDuration time.Duration
	// All build actions from this build.
	All []action
}

// constructGraph constructs a ninjagraph based on files from input paths, and
// populates it with information from ninjalog and compdb.
//
// Steps used to populate the graph are also returned so they can be used in
// later steps.
func constructGraph(ins inputs) (ninjagraph.Graph, error) {
	njl, err := ninjalog.Parse(*ninjalogPath, ins.ninjalog)
	if err != nil {
		return ninjagraph.Graph{}, fmt.Errorf("parsing ninjalog: %v", err)
	}
	steps := ninjalog.Dedup(njl.Steps)

	commands, err := compdb.Parse(ins.compdb)
	if err != nil {
		return ninjagraph.Graph{}, fmt.Errorf("parsing compdb: %v", err)
	}
	steps = ninjalog.Populate(steps, commands)

	graph, err := ninjagraph.FromDOT(ins.graph)
	if err != nil {
		return ninjagraph.Graph{}, fmt.Errorf("parsing Ninja graph: %v", err)
	}
	if err := graph.PopulateEdges(steps); err != nil {
		return ninjagraph.Graph{}, fmt.Errorf("populating graph edges with build steps: %v", err)
	}
	return graph, nil
}

type graph interface {
	PopulatedSteps() ([]ninjalog.Step, error)
}

func extractBuildStats(g graph) (buildStats, error) {
	steps, err := g.PopulatedSteps()
	if err != nil {
		return buildStats{}, fmt.Errorf("getting steps with float and drag: %w", err)
	}

	ret := buildStats{}
	for _, step := range steps {
		if step.OnCriticalPath {
			ret.CriticalPath = append(ret.CriticalPath, toAction(step))
		}
	}
	sort.Slice(ret.CriticalPath, func(i, j int) bool { return ret.CriticalPath[i].Start < ret.CriticalPath[j].Start })

	for _, step := range ninjalog.SlowestSteps(steps, 30) {
		ret.Slowests = append(ret.Slowests, toAction(step))
	}

	for _, step := range steps {
		ret.All = append(ret.All, toAction(step))

		ret.TotalBuildTime += step.Duration()
		// The first action always starts at time zero, so build duration equals to
		// the end time of the last action.
		if step.End > ret.BuildDuration {
			ret.BuildDuration = step.End
		}
	}

	for _, stat := range ninjalog.StatsByType(steps, nil, func(s ninjalog.Step) string { return s.Category() }) {
		var minBuildTime, maxBuildTime time.Duration
		for i, t := range stat.Times {
			if i == 0 {
				minBuildTime, maxBuildTime = t, t
				continue
			}
			if t < minBuildTime {
				minBuildTime = t
			}
			if t > maxBuildTime {
				maxBuildTime = t
			}
		}
		ret.CatBuildTimes = append(ret.CatBuildTimes, catBuildTime{
			Category:     stat.Type,
			Count:        stat.Count,
			BuildTime:    stat.Time,
			MinBuildTime: minBuildTime,
			MaxBuildTime: maxBuildTime,
		})
	}
	return ret, nil
}

func toAction(s ninjalog.Step) action {
	a := action{
		Outputs:    append(s.Outs, s.Out),
		Start:      s.Start,
		End:        s.End,
		Category:   s.Category(),
		TotalFloat: s.TotalFloat,
		Drag:       s.Drag,
	}
	if s.Command != nil {
		a.Command = s.Command.Command
	}
	// TODO(jayzhuang): populate `Rule` when they are made available on step or
	// command.
	return a
}

func serializeBuildStats(s buildStats, w io.Writer) error {
	return json.NewEncoder(w).Encode(s)
}

func main() {
	flag.Parse()

	painter := color.NewColor(colors)
	log := logger.NewLogger(level, painter, os.Stdout, os.Stderr, "")

	if *ninjalogPath == "" {
		log.Fatalf("--ninjalog is required")
	}
	if *compdbPath == "" {
		log.Fatalf("--compdb is required")
	}
	if *graphPath == "" {
		log.Fatalf("--graph is required")
	}
	if *outputPath == "" {
		log.Fatalf("--output is required")
	}

	log.Infof("Reading input files and constructing graph.")
	ninjalog, err := os.Open(*ninjalogPath)
	if err != nil {
		log.Fatalf("Failed to read Ninja log %q: %v", *ninjalogPath, err)
	}
	defer ninjalog.Close()
	compdb, err := os.Open(*compdbPath)
	if err != nil {
		log.Fatalf("Failed to read compdb %q: %v", *compdbPath, err)
	}
	defer compdb.Close()
	graphFile, err := os.Open(*graphPath)
	if err != nil {
		log.Fatalf("Failed to read graph %q: %v", *graphPath, err)
	}
	defer graphFile.Close()
	graph, err := constructGraph(inputs{
		ninjalog: ninjalog,
		compdb:   compdb,
		graph:    graphFile,
	})
	if err != nil {
		log.Fatalf("Failed to construct graph: %v", err)
	}

	log.Infof("Extracting build stats from graph.")
	stats, err := extractBuildStats(&graph)
	if err != nil {
		log.Fatalf("Failed to extract build stats from graph: %v", err)
	}

	log.Infof("Creating %s and serializing the build stats to it.", *outputPath)
	outputFile, err := os.Create(*outputPath)
	if err != nil {
		log.Fatalf("Failed to create output file: %v", err)
	}
	defer func() {
		if err := outputFile.Close(); err != nil {
			log.Errorf("Failed to close output file: %v", err)
		}
	}()

	if err := serializeBuildStats(stats, outputFile); err != nil {
		log.Fatalf("Failed to serialize build stats: %v", err)
	}
	log.Infof("Done.")
}
