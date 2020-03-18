// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"math"
	"os"
	"sort"

	"fuchsia.googlesource.com/benchmarking"
)

const OneSecInUsecs float64 = 1000000
const OneMsecInUsecs float64 = 1000

var (
	verbose = false
)

func check(e error) {
	if e != nil {
		panic(e)
	}
}

// Sorting helpers.
type ByStartTime []*benchmarking.Event

func (a ByStartTime) Len() int           { return len(a) }
func (a ByStartTime) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByStartTime) Less(i, j int) bool { return a[i].Start < a[j].Start }

func getFollowingEvents(event *benchmarking.Event) []*benchmarking.Event {
	visited := make(map[*benchmarking.Event]interface{})
	frontier := make([]*benchmarking.Event, 1)
	frontier[0] = event

	for len(frontier) > 0 {
		current := frontier[len(frontier)-1]
		frontier = frontier[:len(frontier)-1]
		if _, found := visited[current]; found {
			continue
		}
		visited[current] = nil

		for _, child := range current.Children {
			frontier = append(frontier, child)
		}

		if current.Type == benchmarking.FlowEvent && current.Parent != nil {
			frontier = append(frontier, current.Parent)
		}
	}

	result := make([]*benchmarking.Event, 0)
	for e := range visited {
		result = append(result, e)
	}

	sort.Sort(ByStartTime(result))

	return result
}

// Compute the average of an array of floats.
func computeAverage(array []float64) float64 {
	result := 0.0
	for _, item := range array {
		result += item
	}
	return result / float64(len(array))
}

// Compute the maximum element of |array|.
func computeMax(array []float64) float64 {
	result := -math.MaxFloat64
	for _, item := range array {
		if item > result {
			result = item
		}
	}
	return result
}

// Compute the |targetPercentile|th percentile of |array|.
func computePercentile(array []float64, targetPercentile int) float64 {
	if len(array) == 0 {
		panic("Cannot compute the percentile of an empty array")
	}
	sort.Float64s(array)
	if targetPercentile == 100 {
		return array[len(array)-1]
	}
	indexAsFloat, fractional := math.Modf((float64(len(array)) - 1.0) * (0.01 * float64(targetPercentile)))
	index := int(indexAsFloat)
	if len(array) == index+1 {
		return array[index]
	}
	return array[index]*(1-fractional) + array[index+1]*fractional
}

type catAndName struct {
	Cat  string
	Name string
}

func reportInputLatency(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile) {
	startingDurations := make([]*benchmarking.Event, 0)
	for _, p := range model.Processes {
		for _, t := range p.Threads {
			for _, e := range t.Events {
				if e.Cat == "input" && e.Name == "presentation_on_event" {
					startingDurations = append(startingDurations, e)
				}
			}
		}
	}

	totalInputLatencyValues := make([]float64, 0)

	for _, se := range startingDurations {
		followingEvents := getFollowingEvents(se)
		// Only process input flows that resulted in a vsync.
		hasVsync := false
		for _, fe := range followingEvents {
			if fe.Cat == "gfx" && fe.Name == "Display::Controller::OnDisplayVsync" {
				hasVsync = true
				break
			}
		}

		if !hasVsync {
			continue
		}

		for _, event := range followingEvents {
			if event.Name == "Display::Controller::OnDisplayVsync" {
				totalInputLatencyValues = append(totalInputLatencyValues, (event.Start-se.Start)/1000.0)
				break
			}
		}
	}

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "total_input_latency",
		TestSuite: testSuite,
		Unit:      benchmarking.Milliseconds,
		Values:    totalInputLatencyValues,
	})

	if len(totalInputLatencyValues) > 0 {
		mean := computeAverage(totalInputLatencyValues)
		median := computePercentile(totalInputLatencyValues, 50)
		max := computeMax(totalInputLatencyValues)

		fmt.Printf("Computed %d total input latency values\n", len(totalInputLatencyValues))
		fmt.Printf("Total input latency mean: %.4gms\n", mean)
		fmt.Printf("Total input latency median: %.4gms\n", median)
		fmt.Printf("Total input latency max: %.4gms\n", max)
	} else {
		panic("Error, computed 0 total input latency values\n")
	}
}

func main() {
	// Argument handling.
	verbosePtr := flag.Bool("v", false, "Run with verbose logging")
	testSuitePtr := flag.String("test_suite_name", "", "Optional: The name of the test suite.")
	outputFilenamePtr := flag.String("benchmarks_out_filename", "", "Optional: A file to output results to.")

	flag.Parse()
	if flag.NArg() == 0 {
		flag.Usage()
		println("  trace_filename: The input trace file.")
		os.Exit(1)
	}

	verbose = *verbosePtr
	inputFilename := flag.Args()[0]
	testSuite := *testSuitePtr
	outputFilename := *outputFilenamePtr

	traceFile, err := ioutil.ReadFile(inputFilename)
	check(err)

	// Creating the trace model.
	var model benchmarking.Model
	model, err = benchmarking.ReadTrace(traceFile)
	check(err)

	var testResultsFile benchmarking.TestResultsFile
	reportInputLatency(model, testSuite, &testResultsFile)

	if outputFilename != "" {
		outputFile, err := os.Create(outputFilename)
		if err != nil {
			log.Fatalf("failed to create file %s", outputFilename)
		}

		if err := testResultsFile.Encode(outputFile); err != nil {
			log.Fatalf("failed to write results to %s: %v", outputFilename, err)
		}
		if err := outputFile.Close(); err != nil {
			log.Fatalf("failed to close results file %s: %v", outputFilename, err)
		}

		fmt.Printf("\n\nWrote benchmark values to file '%s'.\n", outputFilename)
	}
}
