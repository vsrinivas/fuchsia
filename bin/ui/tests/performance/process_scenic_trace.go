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
type Trace struct {
	TraceEvents     []TraceEvent
	DisplayTimeUnit string
}

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
// the given events, which must be sorted.
func calculateFps(sortedEvents []TraceEvent) (fps float64, fpsPerWindow []float64) {
	baseTime := sortedEvents[0].Ts
	lastEventTime := sortedEvents[len(sortedEvents)-1].Ts

	// window = one-second time window
	const WindowLength float64 = OneSecInUsecs
	fpsPerWindow = make([]float64, 0)
	windowEndTime := baseTime + WindowLength

	numFramesInWindow := 0.0
	numFrames := 0.0

	for _, event := range sortedEvents {
		name := event.Name
		if name == "FramePresented" {
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

// Go JSON encoder will not work if any of the values are NaN, so use 0 in those
// cases instead.
func jsonFloat(num float64) float64 {
	if math.IsNaN(num) || math.IsInf(num, 0) {
		return 0
	} else {
		return num
	}
}

func main() {

	// Argument handling.
	verbosePtr := flag.Bool("v", false, "verbose mode")
	flag.Parse()

	verbose = *verbosePtr

	if len(flag.Args()) < 3 {
		log.Fatal("Usage: process_scenic_trace test_suite_name trace_file benchmark_out_file_name")
	}
	testSuite := flag.Args()[0]
	inputFilename := flag.Args()[1]
	outputFilename := flag.Args()[2]
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

	// Get a list of the duration events. |events| must be in its original order
	// before calling this; sorting can cause the ordering requirements to be
	// violated.
	durations := calculateEventDurations(events)

	// Sort in order of increasing timestamp.
	sort.Sort(ByTimestamp(events))

	// Scrub events with title 'log'; they mess up the total timeline duration
	// because they include events that happen way past the tracing duration.
	filteredEvents := make([]TraceEvent, 0)
	for _, event := range events {
		if !(event.Cat == "" && event.Name == "log") {
			filteredEvents = append(filteredEvents, event)
		}
	}
	events = filteredEvents

	// Calculate FPS. Requires sorted list of events.
	fmt.Printf("== FPS ==\n")
	fps, fpsPerTimeWindow := calculateFps(events)
	fmt.Printf("%.4gfps\nfps per one-second window: %v\n", fps, fpsPerTimeWindow)

	unitName := benchmarking.Milliseconds

	var results benchmarking.TestResultsFile
	results.Add(&benchmarking.TestCaseResults{
		Label:     "fps",
		TestSuite: testSuite,
		Unit:      benchmarking.Unit(unitName),
		Values:    []float64{jsonFloat(fps)},
	})

	results.Add(&benchmarking.TestCaseResults{
		Label:     "fps_per_one_second_time_window",
		TestSuite: testSuite,
		Unit:      benchmarking.Unit(unitName),
		Values:    fpsPerTimeWindow,
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
		results.Add(&benchmarking.TestCaseResults{
			Label:     e.Label,
			TestSuite: testSuite,
			Unit:      benchmarking.Unit(unitName),
			Values:    []float64{avgDuration},
		})
	}
	fmt.Printf("%-35s %.4gms", "unaccounted (mostly gfx driver)",
		jsonFloat(avgDurationBetween(
			events, "gfx", "RenderFrame", "gfx", "Scenic Compositor")/OneMsecInUsecs))

	outputFile, err := os.Create(outputFilename)
	if err != nil {
		log.Fatalf("failed to create file %s", outputFilename)
	}

	if err := results.Encode(outputFile); err != nil {
		log.Fatalf("failed to write results to %s: %v", outputFilename, err)
	}

	fmt.Printf("\n\nWrote benchmark values to file '%s'.\n", outputFilename)
}
