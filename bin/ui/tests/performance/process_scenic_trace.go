// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// process_scenic_trace.go
//
// Usage:
//
// /pkgfs/packages/scenic_benchmarks/0/bin/process_scenic_trace test_label trace_filename benchmarks_out_filename
//
// test_label = test title (used in output file)
// trace_filename = filename for the trace (input)
// bencharks_out_filename = filename for benchmarks file (output)
//
// The output is a JSON file with benchmark statistics.

package main

import (
	"encoding/json"
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

// Structs for parsing trace files (JSON).

// The root level object found in the trace file.
type Trace struct {
	TraceEvents       []TraceEvent
	SystemTraceEvents SystemTraceEvents
	DisplayTimeUnit   string
}

// A struct that represents objects found within the list at the "traceEvents"
// field of the root trace object.  Note that the "Dur" field not actually
// found in the original JSON, it something that we compute later ourselves
// using the timestamps of begin/end events.
//
// Example instance:
// {
//   "args": {
//     "elapsed time since presentation (usecs)": 1,
//     "frame_number": 919,
//     "presentation time (usecs)": 96695880,
//     "target time missed by (usecs)": -3
//   }
//   "cat": "gfx,
//   "name": "FramePresented",
//   "ph": "i",
//   "pid": 9844,
//   "s": "p",
//   "tid": 9862,
//   "ts": 96695884.33333333
// }
type TraceEvent struct {
	Cat  string
	Name string
	Ph   string
	Pid  int
	Tid  int
	Ts   float64
	Id   int
	Dur  float64
	Args map[string]interface{}
}

// A struct that represents the object found at the "systemTraceEvents" field
// of the root trace object.
type SystemTraceEvents struct {
	Events []SystemTraceEvent
	Type   string
}

// A struct that represents objects within the "events" list in the
// "systemTraceEvents" object.  By Fuchsia trace file conventions, we expect
// it to contain a list of events that are actually meta events describing
// processes and threads.  Additionally, it contains CPU utilization events,
// however those are currently unused.  Because of this, it does not contain
// fields such as timestamp.
//
// Example instance:
// {
//   "name": "pthread_t:0xe85f81c8000",
//   "ph": "t",
//   "pid": 9844,
//   "tid": 63681
// }
type SystemTraceEvent struct {
	Name string
	Pid  int
	Tid  int
	Ph   string
}

type Thread struct {
	pid int
	tid int
}

type CatAndId struct {
	cat string
	id  int
}

func printTraceEvent(e TraceEvent) {
	fmt.Printf(
		"ph=%s  %.2f %-15s %-20s\tpid=%-8d\ttid=%-8d\tid=%-8d\tDur=%.4g\t%s\n",
		e.Ph, e.Ts, e.Cat, e.Name, e.Pid, e.Tid, e.Id, e.Dur, e.Args)
}

// Returns a list with only one event per duration event.
// The |Dur| member of the event is populated with its duration.
func calculateEventDurations(events []TraceEvent) []TraceEvent {
	durations := make([]TraceEvent, 0)
	eventStacks := make(map[Thread][]TraceEvent)
	asyncEvents := make(map[CatAndId]TraceEvent)
	for _, event := range events {
		ph := event.Ph
		thread := Thread{event.Pid, event.Tid}

		if verbose {
			printTraceEvent(event)
		}
		if ph == "X" {
			// Complete event
			durations = append(durations, event)
		} else if ph == "b" {
			// Async begin duration event
			asyncEvents[CatAndId{event.Cat, event.Id}] = event
		} else if ph == "e" {
			// Async end duration event
			beginEvent, ok := asyncEvents[CatAndId{event.Cat, event.Id}]
			if ok {
				if beginEvent.Cat != event.Cat {
					panic("Category for begin and end event does not match")
				}
				if beginEvent.Id != event.Id {
					panic("Id for begin and end event does not match")
				}
				// Set duration on the end event.
				event.Dur = event.Ts - beginEvent.Ts
				mergeArgs(&beginEvent, &event)
				durations = append(durations, event)
			}
		} else if ph == "B" {
			// Begin duration event.
			eventStacks[thread] = append(eventStacks[thread], event)
		} else if ph == "E" {
			// End duration event.
			eventStack := eventStacks[thread]
			if eventStack != nil && len(eventStack) > 0 {
				// Peek at last event
				beginEvent := eventStack[len(eventStack)-1]

				if beginEvent.Cat != event.Cat || beginEvent.Name != event.Name {
					// This is possible since events are not necessarily in
					// chronological order; they are grouped by source. So, when
					// processing a new batch of events, it's possible that we
					// get an end event that didn't have a begin event because
					// we started tracing mid-event.
					eventStacks[thread] = nil
					continue
				}

				// Pop last event from event stack.
				eventStacks[thread] = eventStack[:len(eventStack)-1]

				// Set duration on the end event.
				event.Dur = event.Ts - beginEvent.Ts

				mergeArgs(&beginEvent, &event)
				durations = append(durations, event)
			}
		}
	}

	return durations
}

// Takes the 'args' of event1 and writes all its values to the 'args' of event2,
// and vice versa.
// If event2's 'args' already has a value for a given key, it does not get
// overwritten.
func mergeArgs(event1 *TraceEvent, event2 *TraceEvent) {
	// Merge 'Args' maps of both events.
	if event1.Args != nil && event2.Args == nil {
		event2.Args = event1.Args
	} else if event1.Args != nil && event2.Args != nil {
		for k, v := range event2.Args {
			event1.Args[k] = v
		}
		event2.Args = event1.Args
	}

}

// Returns the overall fps and an arrays of fps (one per one second window) for
// the given events.
func calculateFps(events []TraceEvent, fpsEventCat, fpsEventName string) (fps float64, fpsPerWindow []float64) {
	sortedEvents := make([]TraceEvent, len(events))
	copy(sortedEvents, events)
	sort.Sort(ByTimestamp(sortedEvents))

	baseTime := sortedEvents[0].Ts

	// Find the time of the first frame presented. There may be a few seconds
	// at the beginning with no frames and we want to skip that.
	for _, event := range sortedEvents {
		if event.Cat == fpsEventCat && event.Name == fpsEventName {
			baseTime = event.Ts
			break
		}
	}

	lastEventTime := sortedEvents[len(sortedEvents)-1].Ts

	// window = one-second time window
	const WindowLength float64 = OneSecInUsecs
	fpsPerWindow = make([]float64, 0)
	windowEndTime := baseTime + WindowLength

	numFramesInWindow := 0.0
	numFrames := 0.0

	for _, event := range sortedEvents {
		// Make sure to not double count frame events by only counting events with
		// phase "instant" or "end".
		if event.Cat == fpsEventCat && event.Name == fpsEventName && (event.Ph == "i" || event.Ph == "E") {
			if event.Ts < windowEndTime {
				numFramesInWindow++
				numFrames++
			} else {
				for windowEndTime < event.Ts {
					fpsPerWindow = append(fpsPerWindow, numFramesInWindow)
					windowEndTime += WindowLength
					numFramesInWindow = 0
				}
			}
		}
	}
	for windowEndTime < lastEventTime {
		fpsPerWindow = append(fpsPerWindow, numFramesInWindow)
		windowEndTime += WindowLength
		numFramesInWindow = 0
	}
	fps = float64(numFrames) / ((lastEventTime - baseTime) / OneSecInUsecs)
	return fps, fpsPerWindow
}

// For the given set of |events|, find the average duration of all instances of
// events with matching |cat| and |name|.
func avgDuration(events []TraceEvent, cat string, name string) float64 {
	totalTime := 0.0
	numEvents := 0.0

	for _, e := range events {
		if e.Cat == cat && e.Name == name {
			totalTime += e.Dur
			numEvents += 1
		}
	}
	return totalTime / numEvents
}

// For the given set of |events|, find the average duration between event
// (cat1, name1) and event (cat2, name2).
func avgDurationBetween(events []TraceEvent, cat1 string, name1 string, cat2 string, name2 string) float64 {
	lastEventEndTs := 0.0
	totalTime := 0.0
	numEvents := 0.0

	for _, e := range events {
		if e.Cat == cat2 && e.Name == name2 &&
			lastEventEndTs != 0.0 && e.Ph != "E" && e.Ph != "e" {
			totalTime += e.Ts - lastEventEndTs
			lastEventEndTs = 0.0
			numEvents += 1
		} else if e.Cat == cat1 && e.Name == name1 {
			if e.Ph == "E" || e.Ph == "e" {
				lastEventEndTs = e.Ts
			} else {
				lastEventEndTs = e.Ts + e.Dur
			}
		}

	}
	return totalTime / numEvents
}

// Sorting helpers.
type ByTimestamp []TraceEvent

func (a ByTimestamp) Len() int           { return len(a) }
func (a ByTimestamp) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByTimestamp) Less(i, j int) bool { return a[i].Ts < a[j].Ts }

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
func getPidsOfName(trace Trace, processName string) []int {
	result := make([]int, 0)
	for _, event := range trace.SystemTraceEvents.Events {
		if event.Name == processName {
			result = append(result, event.Pid)
		}
	}
	return result
}

// Return all events in |events| that have name |name|.
func getEventsOfName(events []TraceEvent, name string) []TraceEvent {
	result := make([]TraceEvent, 0)
	for _, event := range events {
		if event.Name == name {
			result = append(result, event)
		}
	}
	return result
}

// Return all events that have pid |pid|.
func getEventsOfPid(events []TraceEvent, pid int) []TraceEvent {
	result := make([]TraceEvent, 0)
	for _, event := range events {
		if event.Pid == pid {
			result = append(result, event)
		}
	}
	return result
}

// Return all events that have tid |tid|.
func getEventsOfTid(events []TraceEvent, tid int) []TraceEvent {
	result := make([]TraceEvent, 0)
	for _, event := range events {
		if event.Tid == tid {
			result = append(result, event)
		}
	}
	return result
}

// Project |events| to just their durations, i.e. |events[i].Dur|.
func extractDurations(events []TraceEvent) []float64 {
	result := make([]float64, 0)
	for _, e := range events {
		result = append(result, e.Dur)
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

func getSystemEventsForPid(trace Trace, pid int) []SystemTraceEvent {
	result := make([]SystemTraceEvent, 0)
	for _, event := range trace.SystemTraceEvents.Events {
		if event.Pid == pid {
			result = append(result, event)
		}
	}
	return result
}

// Compute the FPS within Scenic for |trace|, also writing results to
// |results| if provided.
func reportScenicFps(trace Trace, testSuite string, testResultsFile benchmarking.TestResultsFile) {
	durations := calculateEventDurations(trace.TraceEvents)

	events := trace.TraceEvents

	fmt.Printf("=== Scenic FPS ===\n")
	fps, fpsPerTimeWindow := calculateFps(events, "gfx", "FramePresented")
	fmt.Printf("%.4gfps\nfps per one-second window: %v\n", fps, fpsPerTimeWindow)

	unitName := benchmarking.Milliseconds

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "fps",
		TestSuite: testSuite,
		Unit:      benchmarking.Unit(unitName),
		Values:    []float64{jsonFloat(fps)},
	})

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "minimum_fps_per_one_second_time_window",
		TestSuite: testSuite,
		Unit:      benchmarking.Unit(unitName),
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

	for _, e := range averageEvents {
		avgDuration := jsonFloat(avgDuration(durations, "gfx", e.Name) / OneMsecInUsecs)
		if e.Label == "" {
			e.Label = e.Name
		}
		fmt.Printf("%-35s %.4gms\n", strings.Repeat("  ", e.IndentLevel)+e.Label,
			avgDuration)
		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     e.Label,
			TestSuite: testSuite,
			Unit:      benchmarking.Unit(unitName),
			Values:    []float64{avgDuration},
		})
	}
	fmt.Printf("%-35s %.4gms\n", "unaccounted (mostly gfx driver)",
		jsonFloat(avgDurationBetween(
			events, "gfx", "RenderFrame", "gfx", "Scenic Compositor")/OneMsecInUsecs))
}

func reportFlutterFpsForInstance(trace Trace, testSuite string, testResultsFile benchmarking.TestResultsFile, uiEvents []TraceEvent, gpuEvents []TraceEvent, metricNamePrefix string) {
	fmt.Printf("=== Flutter FPS (%s) ===\n", metricNamePrefix)
	fps, fpsPerTimeWindow := calculateFps(uiEvents, "flutter", "vsync callback")
	fmt.Printf("%.4gfps\nfps per one-second window: %v\n", fps, fpsPerTimeWindow)

	frameEvents := getEventsOfName(uiEvents, "Frame")
	frameDurations := extractDurations(calculateEventDurations(frameEvents))
	rasterizerEvents := getEventsOfName(gpuEvents, "GPURasterizer::Draw")
	rasterizerDurations := extractDurations(calculateEventDurations(rasterizerEvents))
	const buildBudget float64 = 8.0 * 1000 * 1000

	averageFrameBuildTimeMillis := computeAverage(frameDurations)
	worstFrameBuildTimeMillis := computeMax(frameDurations)
	missedFrameBuildBudgetCount := len(filterByThreshold(frameDurations, buildBudget))

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
		fullName := metricNamePrefix + metric.Name
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

func reportFlutterFps(trace Trace, testSuite string, testResultsFile benchmarking.TestResultsFile, flutterAppName string) {
	if flutterAppName != "" {
		// TODO: What does this look like if we aren't running in aot mode?  Not a
		// concern for now, as we only use aot.
		flutterPids := getPidsOfName(trace, "io.flutter.runner.aot")
	FlutterPidsLoop:
		for _, flutterPid := range flutterPids {
			systemEvents := getSystemEventsForPid(trace, flutterPid)
			uiSystemEvents := make([]SystemTraceEvent, 0)
			gpuSystemEvents := make([]SystemTraceEvent, 0)
			for _, se := range systemEvents {
				if se.Tid != 0 && strings.HasPrefix(se.Name, flutterAppName) {
					if strings.HasSuffix(se.Name, ".ui") {
						uiSystemEvents = append(uiSystemEvents, se)
					} else if strings.HasSuffix(se.Name, ".gpu") {
						gpuSystemEvents = append(gpuSystemEvents, se)
					}
				}
			}
			if len(uiSystemEvents) != len(gpuSystemEvents) {
				panic("Unequal uiSystemEvents and gpuSystemEvents lengths")
			}
			for i, uiSystemEvent := range uiSystemEvents {
				gpuSystemEvent := gpuSystemEvents[i]
				uiEvents := getEventsOfTid(trace.TraceEvents, uiSystemEvent.Tid)
				gpuEvents := getEventsOfTid(trace.TraceEvents, gpuSystemEvent.Tid)
				metricNamePrefix := strings.Split(uiSystemEvent.Name, ".")[0]
				reportFlutterFpsForInstance(trace, testSuite, testResultsFile, uiEvents, gpuEvents, metricNamePrefix)
				// TODO: Decide how to handle multiple flutter apps that match the
				// target app name. Just report the first one for now.
				break FlutterPidsLoop
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

	// Parsing input.
	var trace Trace
	err = json.Unmarshal([]byte(traceFile), &trace)
	check(err)

	events := trace.TraceEvents
	if events == nil || len(events) == 0 {
		panic("No events found")
	}

	// Scrub events with title 'log'; they mess up the total timeline duration
	// because they include events that happen way past the tracing duration.
	filteredEvents := make([]TraceEvent, 0)
	for _, event := range events {
		if !(event.Cat == "" && event.Name == "log") {
			filteredEvents = append(filteredEvents, event)
		}
	}
	trace.TraceEvents = filteredEvents

	var testResultsFile benchmarking.TestResultsFile
	reportScenicFps(trace, testSuite, testResultsFile)
	reportFlutterFps(trace, testSuite, testResultsFile, flutterAppName)

	if outputFilename != "" {
		outputFile, err := os.Create(outputFilename)
		if err != nil {
			log.Fatalf("failed to create file %s", outputFilename)
		}

		if err := testResultsFile.Encode(outputFile); err != nil {
			log.Fatalf("failed to write results to %s: %v", outputFilename, err)
		}

		fmt.Printf("\n\nWrote benchmark values to file '%s'.\n", outputFilename)
	}
}
