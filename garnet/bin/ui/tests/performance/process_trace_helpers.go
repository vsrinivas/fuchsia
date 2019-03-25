// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains helper functions for processing traces

package main

import (
	"fmt"
	"math"
	"sort"
	"strings"

	"fuchsia.googlesource.com/benchmarking"
)

const OneSecInUsecs float64 = 1000000
const OneMsecInUsecs float64 = 1000

// ByStartTime sorting helpers
type ByStartTime []*benchmarking.Event

func (a ByStartTime) Len() int           { return len(a) }
func (a ByStartTime) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByStartTime) Less(i, j int) bool { return a[i].Start < a[j].Start }

// Return all pids that start with |prefix|.
func getProcessesWithPrefix(model benchmarking.Model, prefix string) []benchmarking.Process {
	result := make([]benchmarking.Process, 0)
	for _, process := range model.Processes {
		if strings.HasPrefix(process.Name, prefix) {
			result = append(result, process)
		}
	}
	return result
}

// Project |events| to just their durations, i.e. |events[i].Dur|.
func extractDurations(events []*benchmarking.Event) []float64 {
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

// The Go JSON encoder will not work if any of the values are NaN, so use 0 in
// those cases instead.
func jsonFloat(num float64) float64 {
	if math.IsNaN(num) || math.IsInf(num, 0) {
		return 0
	} else {
		return num
	}
}

func getThreadsWithSuffix(process benchmarking.Process, suffix string) []benchmarking.Thread {
	threads := make([]benchmarking.Thread, 0)
	for _, thread := range process.Threads {
		if strings.HasSuffix(thread.Name, suffix) {
			threads = append(threads, thread)
		}
	}
	return threads
}

func getThreadsWithPrefix(process benchmarking.Process, prefix string) []benchmarking.Thread {
	threads := make([]benchmarking.Thread, 0)
	for _, thread := range process.Threads {
		if strings.HasPrefix(thread.Name, prefix) {
			threads = append(threads, thread)
		}
	}
	return threads
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

// Returns the overall fps and an array of fps per one second window for the
// given events.
func calculateFps(model benchmarking.Model, fpsEventCat string, fpsEventName string) (fps float64, fpsPerWindow []float64) {
	fpsEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &fpsEventCat, Name: &fpsEventName})
	if len(fpsEvents) == 0 {
		fmt.Printf("Found no events with Category: %s and Name: %s\n", fpsEventCat, fpsEventName)
		return 0, []float64{}
	}
	return calculateFpsForEvents(fpsEvents)
}

func calculateFpsForEvents(fpsEvents []*benchmarking.Event) (fps float64, fpsPerWindow []float64) {
	events := make([]*benchmarking.Event, len(fpsEvents))
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

func averageGap(events []*benchmarking.Event, cat1 string, name1 string, cat2 string, name2 string) float64 {
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

func threadNameIsUnique(name string, threads []benchmarking.Thread) bool {
	count := 0
	for _, thread := range threads {
		if thread.Name == name {
			count++
		}
	}

	return count == 1
}

func listContainsElement(list []string, element string) bool {
	for _, e := range list {
		if e == element {
			return true
		}
	}

	return false
}
