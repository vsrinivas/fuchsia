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

	"benchmark_suite"

	"fuchsia.googlesource.com/benchmarking"
)

// Structure is:
// fuchsia.fidl_microbenchmarks/[Language]/[Encode or Decode]/[Structure]/[Result]
// Each slash constitutes a subtest.
const testSuite = "fuchsia.fidl_microbenchmarks"

func main() {
	var outputFilename string
	flag.StringVar(&outputFilename, "out_file", "", "optional: A file to output results to.")
	flag.Parse()

	var results benchmarking.TestResultsFile
	for _, b := range benchmark_suite.Benchmarks {
		results = append(results, runBenchmark(b, testSuite)...)
	}

	outputFile := os.Stdout
	if outputFilename != "" {
		var err error
		outputFile, err = os.Create(outputFilename)
		if err != nil {
			log.Fatalf("failed to create file %q: %v", outputFilename, err)
		}
		defer outputFile.Close()
	}

	if err := results.Encode(outputFile); err != nil {
		log.Fatalf("failed to write results: %v", err)
	}

	if outputFilename != "" {
		fmt.Printf("\n\nWrote benchmark values to file %q.\n", outputFilename)
	}
}

func runBenchmark(b benchmark_suite.Benchmark, testSuite string) []*benchmarking.TestCaseResults {
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
