// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"testing"
	"time"

	"benchmark_suite"

	"go.fuchsia.dev/fuchsia/garnet/go/src/benchmarking"
)

// Structure is:
// fuchsia.fidl_microbenchmarks/[Language]/[Encode or Decode]/[Structure]/[Result]
// Each slash constitutes a subtest.
const testSuite = "fuchsia.fidl_microbenchmarks"

// Go default is 1s.
const defaultBenchTime = 1 * time.Second

func isFlagSet(name string) bool {
	isSet := false
	flag.Visit(func(f *flag.Flag) {
		if f.Name == name {
			isSet = true
		}
	})
	return isSet
}

func main() {
	var outputFilename string
	testing.Init()
	flag.StringVar(&outputFilename, "out_file", "", "output results in fuchsiaperf.json format (optional)")
	flag.Parse()

	if !isFlagSet("test.benchtime") {
		flag.Set("test.benchtime", defaultBenchTime.String())
	}

	if outputFilename == "" {
		stdout()
	} else {
		outputFile(outputFilename)
	}
}

func stdout() {
	for _, b := range benchmark_suite.Benchmarks {
		result := testing.Benchmark(b.BenchFunc)
		fmt.Printf("Benchmark%s    %s\n", b.Label, result)
	}
}

func outputFile(outputFilename string) {
	var results benchmarking.TestResultsFile
	for _, b := range benchmark_suite.Benchmarks {
		results = append(results, runFuchsiaPerfBenchmark(b, testSuite)...)
	}

	outputFile, err := os.Create(outputFilename)
	if err != nil {
		log.Fatalf("failed to create file %q: %v", outputFilename, err)
	}
	defer outputFile.Close()

	if err := results.Encode(outputFile); err != nil {
		log.Fatalf("failed to write results: %v", err)
	}
	fmt.Printf("\n\nWrote benchmark values to file %q.\n", outputFilename)
}

func runFuchsiaPerfBenchmark(b benchmark_suite.Benchmark, testSuite string) []*benchmarking.TestCaseResults {
	benchmarkResult := testing.Benchmark(b.BenchFunc)
	return []*benchmarking.TestCaseResults{
		{
			Label:     "Go/" + b.Label + "/WallTime",
			TestSuite: testSuite,
			Unit:      benchmarking.Nanoseconds,
			Values:    []float64{float64(benchmarkResult.NsPerOp())},
		},
		{
			Label:     "Go/" + b.Label + "/Allocations",
			TestSuite: testSuite,
			Unit:      benchmarking.Count,
			Values:    []float64{float64(benchmarkResult.AllocsPerOp())},
		},
		{
			Label:     "Go/" + b.Label + "/AllocatedBytes",
			TestSuite: testSuite,
			Unit:      benchmarking.Bytes,
			Values:    []float64{float64(benchmarkResult.AllocedBytesPerOp())},
		},
	}
}
