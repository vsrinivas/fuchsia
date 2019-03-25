// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Usage:
//
// /pkgfs/packages/scenic_benchmarks/0/bin/process_gfx_trace [-test_suite_name=label] [-benchmarks_out_filename=output_file] [-flutter_app_names=flutter_app_names] [-all_apps=all_apps] trace_filename
//
// output = Optional: A file to output results to.
// flutter_app_names = Optional: Comma separated names of the flutter apps to measure fps for.
// all_apps = Optional: Decide whether to measure fps for all apps. Overrides flutter_app_names.
// label = Optional: The name of the test suite.
// trace_filename = The input trace files.
//
// The output is a JSON file with benchmark statistics.

package main

import (
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"strings"

	"fuchsia.googlesource.com/benchmarking"
)

var (
	verbose = false
)

func check(e error) {
	if e != nil {
		panic(e)
	}
}

// Calculates and reports the latency from a Present() call in Flutter to a ScheduleUpdate() call in Scenic.
// These functions are mapped directly to one another and represent the combined latencies of waiting for
// fences in Flutter, traversing FIDL and waiting for a busy Scenic.
func reportFlutterToScenicLatencies(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile, allFlutterApps bool, flutterAppNames []string) {
	schedulingEventName := "Session::ScheduleUpdate"
	gfxCategory := "gfx"
	schedulingEvents := model.FindEvents(
		benchmarking.EventsFilter{Cat: &gfxCategory, Name: &schedulingEventName})

	flutterInstances := getFlutterInstances(model)
	for _, instance := range flutterInstances {
		if allFlutterApps || listContainsElement(flutterAppNames, instance.flutterName) {
			instance.metricName = getMetricNameForInstance(instance, flutterAppNames)
			reportFlutterPresentToScenicScheduleLatency(testSuite, testResultsFile, instance, schedulingEvents)
		}
	}
}

// Calculates and reports the latency between call to Present() in Flutter to a ScheduleUpdate() call in Scenic for a single Flutter instance.
func reportFlutterPresentToScenicScheduleLatency(testSuite string, testResultsFile *benchmarking.TestResultsFile, instance flutterInstance, updateSchedulingEvents []*benchmarking.Event) {
	presentCallName := "SessionPresent"
	presentCallEvents := instance.gpuThread.FindEvents(benchmarking.EventsFilter{Name: &presentCallName})

	index := 0

	latencies := make([]float64, 0, len(updateSchedulingEvents))

	for _, presentCall := range presentCallEvents {
		callTime := presentCall.Start

		// Find next update schedule
		for index < len(updateSchedulingEvents) &&
			updateSchedulingEvents[index].Start < callTime {
			index++
		}
		if index >= len(updateSchedulingEvents) {
			break
		}
		for index < len(updateSchedulingEvents) &&
			updateSchedulingEvents[index].Args["session_debug_name"] != instance.flutterName {
			index++
		}
		if index >= len(updateSchedulingEvents) {
			break
		}

		schedulingTime := updateSchedulingEvents[index].Start

		latency := schedulingTime - callTime
		latencies = append(latencies, latency)
	}

	// Convert to milliseconds.
	latencies = convertMicrosToMillis(latencies)
	avgPresentToScheduleLatency := computeAverage(latencies)
	minLatency := computeMin(latencies)
	maxLatency := computeMax(latencies)

	fmt.Printf("Flutter app: %s\n", instance.metricName)
	fmt.Printf("Time from Present() in flutter to ScheduleUpdate() in scenic:\n")
	fmt.Printf("	Avg: %.4g ms\n", avgPresentToScheduleLatency)
	fmt.Printf("	Min: %.4g ms\n", minLatency)
	fmt.Printf("	Max: %.4g ms\n\n", maxLatency)

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     instance.metricName + "_flutter_to_scenic_latencies",
		TestSuite: testSuite,
		Unit:      benchmarking.Milliseconds,
		Values:    latencies,
	})
}

func main() {
	// Argument handling.
	verbosePtr := flag.Bool("v", false, "Run with verbose logging.")
	testSuitePtr := flag.String("test_suite_name", "", "Optional: The name of the test suite.")
	outputFilenamePtr := flag.String("benchmarks_out_filename", "", "Optional: A file to output results to.")
	flutterAppNamesPtr := flag.String("flutter_app_names", "", "Optional: Comma separated names of the flutter apps to measure fps for.")
	allApps := flag.Bool("all_apps", false, "Optional: Decide whether to measure fps for all apps. Overrides flutter_app_names.")

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
	flutterAppNames := strings.Split(*flutterAppNamesPtr, ",")
	allFlutterApps := *allApps

	traceFile, err := ioutil.ReadFile(inputFilename)
	check(err)

	// Create the trace model.
	var model benchmarking.Model
	model, err = benchmarking.ReadTrace(traceFile)
	check(err)

	if len(model.Processes) == 0 {
		panic("No processes found in the model\n")
	}

	// Process trace and report.
	var testResultsFile benchmarking.TestResultsFile

	if allFlutterApps || (len(flutterAppNames) > 0 && flutterAppNames[0] != "") {
		fmt.Printf("\n=== Flutter ===\n")
		reportFlutterFps(model, testSuite, &testResultsFile, allFlutterApps, flutterAppNames)
		fmt.Printf("\n=== Flutter to Scenic Latency ===\n")
		reportFlutterToScenicLatencies(model, testSuite, &testResultsFile, allFlutterApps, flutterAppNames)
	}

	fmt.Printf("\n=== Scenic ===\n")
	reportScenicFps(model, testSuite, &testResultsFile, allFlutterApps, flutterAppNames)

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
