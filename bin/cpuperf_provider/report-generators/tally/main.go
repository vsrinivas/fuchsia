// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"math"
	"os"
	"sort"
	"strings"

	humanize "github.com/dustin/go-humanize"
)

type EventArgs struct {
	Value uint64
}

type Event struct {
	Args EventArgs
	Cat  string
	Id   string
	Name string
	Ph   string
	Tid  uint
	Ts   float64
}

type TraceJson struct {
	TraceEvents []Event
}

// This is used to compute the lapsed time of the count: We're given two
// records: one at start-time and one at stop-time.
type EventKey struct {
	Cat string
	Id  string
	Ph  string
	Tid uint
}

func makeEventKey(event Event) EventKey {
	return EventKey{event.Cat,
		event.Id,
		event.Ph,
		event.Tid}
}

type CpuRecord struct {
	Name     string
	Id       string
	Value    uint64
	Duration float64
}

// This is for sorting CpuRecord by ids.
type CpuRecordArray []CpuRecord

func (a CpuRecordArray) Len() int           { return len(a) }
func (a CpuRecordArray) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a CpuRecordArray) Less(i, j int) bool { return a[i].Id < a[j].Id }

func commaFormat(n uint64) string {
	// humanize takes int64s. If we get a number that big don't print
	// garbage, just don't comma-ize it.
	if n > math.MaxInt64 {
		return fmt.Sprintf("%d", n)
	}
	return humanize.Comma(int64(n))
}

var (
	outputFile string
	title      string
)

func init() {
	flag.StringVar(&outputFile, "output", "", "Where to write the result to")
	flag.StringVar(&title, "title", "Table of events", "The title of the report")
}

func main() {
	log.SetFlags(0)
	log.SetPrefix("tally: ")
	log.Print("started")

	flag.Parse()

	if flag.NArg() > 1 {
		log.Fatal("Too many arguments")
	}

	inputFile := flag.Arg(0)

	var in io.Reader
	var out io.Writer
	if inputFile != "" {
		f, err := os.Open(inputFile)
		if err != nil {
			log.Fatalln("Unable to open input file:", err)
		}
		in = io.Reader(f)
		defer f.Close()
	} else {
		in = os.Stdin
	}
	if outputFile != "" {
		f, err := os.Create(outputFile)
		if err != nil {
			log.Fatalln("Unable to open output file:", err)
		}
		out = io.Writer(f)
		defer f.Close()
	} else {
		out = os.Stdout
	}

	decoder := json.NewDecoder(in)
	var json TraceJson
	if err := decoder.Decode(&json); err != nil {
		log.Fatalln("Error reading json input:", err)
		return
	}

	fmt.Fprintf(out, "%s\n", title)

	startEvents := make(map[EventKey]Event)
	endEvents := make(map[EventKey]Event)
	perCpuRecords := make(map[uint]CpuRecordArray)

	// First pass: Collect data for each cpu.
	for _, event := range json.TraceEvents {
		if !strings.HasPrefix(event.Cat, "cpu:") {
			continue
		}
		key := makeEventKey(event)
		start, start_ok := startEvents[key]
		if !start_ok {
			startEvents[key] = event
			continue
		}
		// TODO(dje): For now we assume there are only two records
		// for each event: start and end. Instead accumulate the
		// results.
		_, end_ok := endEvents[key]
		if end_ok {
			log.Println("WARNING: unexpected extra record")
			continue
		}
		endEvents[key] = event
		cpu := event.Tid
		perCpuRecords[cpu] = append(perCpuRecords[cpu], CpuRecord{
			event.Name,
			event.Id,
			event.Args.Value - start.Args.Value,
			event.Ts - start.Ts})
	}

	// Second pass: Print

	var cpus []uint
	for k := range perCpuRecords {
		cpus = append(cpus, k)
	}
	sort.Slice(cpus, func(i, j int) bool { return cpus[i] < cpus[j] })
	for _, cpu := range cpus {
		if cpu == 0 {
			fmt.Fprintf(out, "System-wide\n")
		} else {
			fmt.Fprintf(out, "Cpu %d\n", cpu-1)
		}
		records := perCpuRecords[cpu]
		sort.Sort(records)
		for _, record := range records {
			value_string := fmt.Sprintf("%16s", commaFormat(record.Value))
			if record.Duration != 0 {
				microsecsPerSec := uint64(1000 * 1000)
				value_string += fmt.Sprintf(
					" (%s/sec)",
					commaFormat((record.Value*microsecsPerSec)/uint64(record.Duration)))
			}
			fmt.Fprintf(out, "  %-38s %s\n",
				value_string, record.Name)
		}
	}

	log.Print("done")
	os.Exit(0)
}
