// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This Go utility takes in a JSON tracing file and converts it to a CSV file.
// This CSV file includes information needed for a playback utility
// //zircon/system/uapp/blk-playback/blk-playback.c which re-creates
// events that have previously occured on a block device.

package main

import (
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"strconv"
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

func checkError(message string, err error) {
	if err != nil {
		log.Fatal(message, err)
	}
}

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

func convert(duration []TraceEvent) [][]string {
	outputData := make([][]string, 0, len(duration)+1)

	// Add columm headings
	outputData = append(outputData, []string{
		"category",
		"name",
		"start_time",
		"end_time",
		"duration",
		"tid",
		"length",
		"offset_dev",
		"command",
		"offset_vmo",
	})

	for _, event := range duration {
		outputData = append(outputData, []string{
			event.Cat,
			event.Name,
			strconv.FormatFloat(event.Ts, 'f', -1, 64),
			strconv.FormatFloat(event.Ts+event.Dur, 'f', -1, 64),
			strconv.FormatFloat(event.Dur, 'f', -1, 64),
			strconv.Itoa(event.Tid),
			strconv.Itoa(int(event.Args["length"].(float64))),
			strconv.Itoa(int(event.Args["offset_dev"].(float64))),
			strconv.Itoa(int(event.Args["command"].(float64))),
			strconv.Itoa(int(event.Args["offset_vmo"].(float64))),
		})
	}
	return outputData
}

// Returns a list with only one event per duration event.
// The |Dur| member is populated with duration.
func calculateEventDurations(events []TraceEvent, category string) []TraceEvent {
	durations := make([]TraceEvent, 0)
	eventStacks := make(map[Thread][]TraceEvent)
	asyncEvents := make(map[CatAndId]TraceEvent)
	for _, event := range events {
		ph := event.Ph
		thread := Thread{event.Pid, event.Tid}
		cat := event.Cat

		if verbose {
			printTraceEvent(event)
		}
		if cat != category {
			continue
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

type ByTimestamp []TraceEvent

func (a ByTimestamp) Len() int           { return len(a) }
func (a ByTimestamp) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByTimestamp) Less(i, j int) bool { return a[i].Ts < a[j].Ts }

func main() {
	verbosePtr := flag.Bool("v", false, "verbose mode")
	flag.Parse()

	verbose = *verbosePtr

	if len(flag.Args()) != 3 {
		log.Fatal("Usage: driver_trace_parsing trace_category trace_in_file_name parsing_out_file_name")
	}
	category := flag.Args()[0]
	inputFilename := flag.Args()[1]
	outputFilename := flag.Args()[2]
	traceFile, err := ioutil.ReadFile(inputFilename)
	check(err)

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
	durations := calculateEventDurations(events, category)

	// Convert trace events to the actual output data
	outputData := convert(durations)
	fmt.Printf("Received %d tracing records.\n", len(outputData))

	outputFile, err := os.Create(outputFilename)
	checkError("Cannot create file", err)
	defer outputFile.Close()

	writer := csv.NewWriter(outputFile)
	defer writer.Flush()

	for _, value := range outputData {
		err := writer.Write(value)
		checkError("Cannot write to file", err)
	}

	fmt.Printf("Wrote output to csv file '%s'.\n", outputFilename)
}
