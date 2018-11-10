// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// process_scenic_trace.go
//
// Usage:
//
// /pkgfs/packages/scenic_benchmarks/0/bin/process_scenic_trace [-test_suite_name=label] [-benchmarks_out_filename=output_file] [-flutter_app_name=app_name] trace_filename
//
// output = Optional: A file to output results to.
// app_name = Optional: The name of the flutter app to measure fps for.
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
	"math"
	"os"
	"sort"
	"strings"

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
type ByStartTime []benchmarking.Event

func (a ByStartTime) Len() int           { return len(a) }
func (a ByStartTime) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByStartTime) Less(i, j int) bool { return a[i].Start < a[j].Start }

func calculateFpsForEvents(fpsEvents []benchmarking.Event) (fps float64, fpsPerWindow []float64) {
	events := make([]benchmarking.Event, len(fpsEvents))
	copy(events, fpsEvents)
	sort.Sort(ByStartTime(events))
	baseTime := events[0].Start

	// window = one-second time window
	const WindowLength float64 = OneSecInUsecs
	fpsPerWindow = make([]float64, 0)
	windowEndTime := baseTime + WindowLength

	numFramesInWindow := 0.0
	numFrames := 0.0

	for _, event := range events {
		if event.Start < windowEndTime {
			numFramesInWindow++
			numFrames++
		} else {
			for windowEndTime < event.Start {
				fpsPerWindow = append(fpsPerWindow, numFramesInWindow)
				windowEndTime += WindowLength
				numFramesInWindow = 0
			}
		}
	}
	lastEventTime := events[len(events)-1].Start

	for windowEndTime < lastEventTime {
		fpsPerWindow = append(fpsPerWindow, numFramesInWindow)
		windowEndTime += WindowLength
		numFramesInWindow = 0
	}
	fps = float64(numFrames) / ((lastEventTime - baseTime) / OneSecInUsecs)
	return fps, fpsPerWindow
}

// Returns the overall fps and an array of fps per one second window for the
// given events.
func calculateFps(model benchmarking.Model, fpsEventCat string, fpsEventName string) (fps float64, fpsPerWindow []float64) {
	fpsEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &fpsEventCat, Name: &fpsEventName})
	return calculateFpsForEvents(fpsEvents)
}

// The Go JSON encoder will not work if any of the values are NaN, so use 0 in
// those cases instead.
func jsonFloat(num float64) float64 {
	if math.IsNaN(num) || math.IsInf(num, 0) {
		return 0
	} else {
		return num
	}
}

// Return all pids that have name |processName|.
func getProcessesWithName(model benchmarking.Model, processName string) []benchmarking.Process {
	result := make([]benchmarking.Process, 0)
	for _, process := range model.Processes {
		if process.Name == processName {
			result = append(result, process)
		}
	}
	return result
}

// Project |events| to just their durations, i.e. |events[i].Dur|.
func extractDurations(events []benchmarking.Event) []float64 {
	result := make([]float64, len(events))
	for i, e := range events {
		result[i] = e.Dur
	}
	return result
}

// Convert an array of values in microseconds to milliseconds.
func convertMicrosToMillis(array []float64) []float64 {
	result := make([]float64, len(array))
	for i, item := range array {
		result[i] = item / OneMsecInUsecs
	}
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

// Compute the minimum element of |array|.
func computeMin(array []float64) float64 {
	result := math.MaxFloat64
	for _, item := range array {
		if item < result {
			result = item
		}
	}
	return result
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

// [x for x in array if x >= threshold]
func filterByThreshold(array []float64, threshold float64) []float64 {
	result := make([]float64, 0)
	for _, item := range array {
		if item >= threshold {
			result = append(result, item)
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

func averageGap(events []benchmarking.Event, cat1 string, name1 string, cat2 string, name2 string) float64 {
	gapStart := 0.0
	totalTime := 0.0
	numOfGaps := 0.0

	for _, event := range events {
		if event.Cat == cat1 && event.Name == name1 {
			gapStart = event.Start + event.Dur
		} else if event.Cat == cat2 && event.Name == name2 &&
			gapStart != 0.0 {
			if event.Start > gapStart {
				totalTime += (event.Start - gapStart)
				numOfGaps++
			}
			gapStart = 0.0
		}
	}
	return totalTime / numOfGaps
}

// Compute the FPS within Scenic for |trace|, also writing results to
// |results| if provided.
func reportScenicFps(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile) {
	fmt.Printf("=== Scenic FPS ===\n")
	fps, fpsPerTimeWindow := calculateFps(model, "gfx", "FramePresented")
	fmt.Printf("%.4gfps\nfps per one-second window: %v\n", fps, fpsPerTimeWindow)

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "fps",
		TestSuite: testSuite,
		Unit:      benchmarking.FramesPerSecond,
		Values:    []float64{jsonFloat(fps)},
	})

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "minimum_fps_per_one_second_time_window",
		TestSuite: testSuite,
		Unit:      benchmarking.FramesPerSecond,
		Values:    []float64{jsonFloat(computeMin(fpsPerTimeWindow))},
	})

	fmt.Printf("\n== Average times ==\n")
	type AverageEvent struct {
		IndentLevel int
		Name        string
		Label       string
	}
	averageEvents := []AverageEvent{
		{0, "RenderFrame", ""},
		{1, "ApplyScheduledSessionUpdates", ""},
		{2, "escher::CommandBuffer::Submit", "CommandBuffer::Submit"},
		{1, "UpdateAndDeliverMetrics", ""},
		{1, "Compositor::DrawFrame", ""},
		{0, "Scenic Compositor", "Escher GPU time"},
	}

	gfxStr := "gfx"
	for _, e := range averageEvents {
		events := model.FindEvents(benchmarking.EventsFilter{Cat: &gfxStr, Name: &e.Name})
		avgDuration := jsonFloat(benchmarking.AvgDuration(events) / OneMsecInUsecs)
		if e.Label == "" {
			e.Label = e.Name
		}
		fmt.Printf("%-35s %.4gms\n", strings.Repeat("  ", e.IndentLevel)+e.Label,
			avgDuration)
		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     e.Label,
			TestSuite: testSuite,
			Unit:      benchmarking.Milliseconds,
			Values:    []float64{avgDuration},
		})
	}
	renderFrameStr := "RenderFrame"
	scenicCompositorStr := "Scenic Compositor"
	events := model.FindEvents(benchmarking.EventsFilter{Cat: &gfxStr, Name: &renderFrameStr})
	events = append(events, model.FindEvents(benchmarking.EventsFilter{Cat: &gfxStr, Name: &scenicCompositorStr})...)
	sort.Sort(ByStartTime(events))

	fmt.Printf("%-35s %.4gms\n", "unaccounted (mostly gfx driver)",
		jsonFloat(averageGap(events, gfxStr, renderFrameStr, gfxStr, scenicCompositorStr)/OneMsecInUsecs))
}

func reportFlutterFpsForInstance(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile, uiThread benchmarking.Thread, gpuThread benchmarking.Thread, metricNamePrefix string) {
	fmt.Printf("=== Flutter FPS (%s) ===\n", metricNamePrefix)
	flutterStr := "flutter"
	vsyncCallbackStr := "vsync callback"
	vsyncEvents := uiThread.FindEvents(benchmarking.EventsFilter{Cat: &flutterStr, Name: &vsyncCallbackStr})
	fps, fpsPerTimeWindow := calculateFpsForEvents(vsyncEvents)
	fmt.Printf("%.4gfps\nfps per one-second window: %v\n", fps, fpsPerTimeWindow)

	frameStr := "Frame"
	frameEvents := uiThread.FindEvents(benchmarking.EventsFilter{Name: &frameStr})

	frameDurations := convertMicrosToMillis(extractDurations(frameEvents))

	const buildBudget float64 = 8.0

	averageFrameBuildTimeMillis := computeAverage(frameDurations)
	worstFrameBuildTimeMillis := computeMax(frameDurations)
	missedFrameBuildBudgetCount := len(filterByThreshold(frameDurations, buildBudget))

	drawStr := "GPURasterizer::Draw"
	rasterizerEvents := gpuThread.FindEvents(benchmarking.EventsFilter{Name: &drawStr})
	rasterizerDurations := convertMicrosToMillis(extractDurations(rasterizerEvents))

	averageFrameRasterizerTimeMillis := computeAverage(rasterizerDurations)
	percentile90FrameRasterizerTimeMillis := computePercentile(rasterizerDurations, 90)
	percentile99FrameRasterizerTimeMillis := computePercentile(rasterizerDurations, 99)
	worstFrameRasterizerTimeMillis := computeMax(rasterizerDurations)
	missedFrameRasterizerBudgetCount := len(filterByThreshold(rasterizerDurations, buildBudget))

	type Metric struct {
		Name   string
		Values []float64
	}

	// Metrics inspired by https://github.com/flutter/flutter/blob/master/packages/flutter_driver/lib/src/driver/timeline_summary.dart.
	metrics := []Metric{
		{"fps", []float64{fps}},
		{"average_frame_build_time_millis", []float64{averageFrameBuildTimeMillis}},
		{"worst_frame_build_time_millis", []float64{worstFrameBuildTimeMillis}},
		{"missed_frame_build_budget_count", []float64{float64(missedFrameBuildBudgetCount)}},
		{"average_frame_rasterizer_time_millis", []float64{averageFrameRasterizerTimeMillis}},
		{"percentile_90_frame_rasterizer_time_millis", []float64{percentile90FrameRasterizerTimeMillis}},
		{"percentile_99_frame_rasterizer_time_millis", []float64{percentile99FrameRasterizerTimeMillis}},
		{"worst_frame_rasterizer_time_millis", []float64{worstFrameRasterizerTimeMillis}},
		{"missed_frame_rasterizer_budget_count", []float64{float64(missedFrameRasterizerBudgetCount)}},
		{"frame_count", []float64{float64(len(frameDurations))}},
		{"frame_build_times", frameDurations},
		{"frame_rasterizer_times", rasterizerDurations},
	}
	for _, metric := range metrics {
		fullName := metricNamePrefix + "_" + metric.Name
		// Only print scalar metrics, as the collection based ones are too large
		// to be useful when printed out.
		if len(metric.Values) == 1 {
			fmt.Printf("%s: %4g\n", fullName, metric.Values)
		}
		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     fullName,
			TestSuite: testSuite,
			Unit:      benchmarking.Unit(benchmarking.Milliseconds),
			Values:    metric.Values,
		})
	}
}

func getThreadsWithPrefixAndSuffix(process benchmarking.Process, prefix string, suffix string) []benchmarking.Thread {
	threads := make([]benchmarking.Thread, 0)
	for _, thread := range process.Threads {
		if strings.HasPrefix(thread.Name, prefix) && strings.HasSuffix(thread.Name, suffix) {
			threads = append(threads, thread)
		}
	}
	return threads
}

func reportFlutterFps(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile, flutterAppName string) {
	if flutterAppName != "" {
		// TODO: What does this look like if we aren't running in aot mode?  Not a
		// concern for now, as we only use aot.
		flutterProcesses := getProcessesWithName(model, "io.flutter.runner.aot")
	FlutterProcessesLoop:
		for _, flutterProcess := range flutterProcesses {
			gpuThreads := getThreadsWithPrefixAndSuffix(flutterProcess, flutterAppName, ".gpu")
			uiThreads := getThreadsWithPrefixAndSuffix(flutterProcess, flutterAppName, ".ui")
			if len(gpuThreads) != len(uiThreads) {
				panic("Unequal ui threads and gpu threads")
			}

			for i, uiThread := range uiThreads {
				// TODO: We are assuming that threads are in order, instead we should verify
				// that they have the same name % suffix
				gpuThread := gpuThreads[i]
				metricNamePrefix := strings.Split(uiThread.Name, ".")[0]
				reportFlutterFpsForInstance(model, testSuite, testResultsFile, uiThread, gpuThread, metricNamePrefix)
				// TODO: Decide how to handle multiple flutter apps that match the
				// target app name. Just report the first one for now.
				break FlutterProcessesLoop
			}
		}
	}
}

func main() {
	// Argument handling.
	verbosePtr := flag.Bool("v", false, "Run with verbose logging")
	flutterAppNamePtr := flag.String("flutter_app_name", "", "Optional: The name of the flutter app to measure fps for.")
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
	flutterAppName := *flutterAppNamePtr
	testSuite := *testSuitePtr
	outputFilename := *outputFilenamePtr

	traceFile, err := ioutil.ReadFile(inputFilename)
	check(err)

	// Creating the trace model.
	var model benchmarking.Model
	model, err = benchmarking.ReadTrace(traceFile)
	check(err)

	if len(model.Processes) == 0 {
		panic("No processes found in the model")
	}

	var testResultsFile benchmarking.TestResultsFile
	reportScenicFps(model, testSuite, &testResultsFile)
	reportFlutterFps(model, testSuite, &testResultsFile, flutterAppName)

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
