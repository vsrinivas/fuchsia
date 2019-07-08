// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains helper functions for process_gfx_trace.go for measuring latency and fps of flutter processes.

package main

import (
	"fmt"
	"strconv"
	"strings"

	"fuchsia.googlesource.com/benchmarking"
)

func getMetricNameForInstance(instance flutterInstance, flutterAppNames []string) string {
	for _, name := range flutterAppNames {
		if instance.flutterName == name {
			return name
		}
	}

	return instance.metricName
}

// Compute the FPS and latency of all Flutter processes in |trace|, also writing results to.
// |testResultsFile| if provided.
func reportFlutterFps(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile, allFlutterApps bool, flutterAppNames []string) {
	instances := getFlutterInstances(model)

	for _, instance := range instances {
		if allFlutterApps || listContainsElement(flutterAppNames, instance.flutterName) {
			if !allFlutterApps {
				instance.metricName = getMetricNameForInstance(instance, flutterAppNames)
			}

			reportFlutterFpsForInstance(model, testSuite, testResultsFile, instance)
		}
	}

}

type flutterInstance struct {
	flutterName string
	metricName  string
	uiThread    benchmarking.Thread
	gpuThread   benchmarking.Thread
}

func getFlutterInstances(model benchmarking.Model) []flutterInstance {
	flutterProcesses := getProcessesWithPrefix(model, "io.flutter")

	var instances []flutterInstance

	for i, flutterProcess := range flutterProcesses {
		gpuThreads := getThreadsWithSuffix(flutterProcess, ".gpu")
		uiThreads := getThreadsWithSuffix(flutterProcess, ".ui")

		for j, uiThread := range uiThreads {
			nameIsUnique := threadNameIsUnique(uiThread.Name, uiThreads)

			metricNamePrefix := strings.Split(uiThread.Name, ".")[0]

			var gpuThread benchmarking.Thread
			for k, thread := range gpuThreads {
				// Assuming ui and gpu threads come in the same order. Remove to prevent using same trace twice.
				if strings.Split(thread.Name, ".")[0] == metricNamePrefix {
					gpuThread = thread
					gpuThreads = append(gpuThreads[:k], gpuThreads[k+1:]...)
					break
				}
			}

			// Extract the process name from the thread: flutter_app.cmx.ui -> flutter_app.cmx
			flutterNameSlice := strings.Split(uiThread.Name, ".")
			flutterNameSlice = flutterNameSlice[:len(flutterNameSlice)-1]
			flutterName := strings.Join(flutterNameSlice, ".")

			// Setup a metric name for the process. Add [processNumber, threadNumber] suffix if there are duplicates.
			metricName := flutterName
			if !nameIsUnique {
				metricName = metricNamePrefix + "_[" + strconv.Itoa(i) + "," + strconv.Itoa(j) + "]"
			}

			instances = append(instances, flutterInstance{flutterName, metricName, uiThread, gpuThread})
		}
	}

	return instances
}

// Reports fps and dropped frames for a flutter process.
func reportFlutterFpsForInstance(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile, instance flutterInstance) {
	fmt.Printf("Flutter app: ")
	fmt.Printf("%s\n", instance.metricName)
	flutterStr := "flutter"
	vsyncCallbackStr := "vsync callback"
	vsyncEvents := instance.uiThread.FindEvents(benchmarking.EventsFilter{Cat: &flutterStr, Name: &vsyncCallbackStr})
	fps := calculateFpsForEvents(vsyncEvents)
	fmt.Printf("%.4g FPS\n", fps)

	if len(instance.gpuThread.Events) == 0 {
		panic("No GPU thread found\n")
	}

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     instance.metricName + "_fps",
		TestSuite: testSuite,
		Unit:      benchmarking.FramesPerSecond,
		Values:    []float64{jsonFloat(fps)},
	})

	frameStr := "vsync callback"
	frameEvents := instance.uiThread.FindEvents(benchmarking.EventsFilter{Name: &frameStr})

	frameDurations := convertMicrosToMillis(extractDurations(frameEvents))

	drawStr := "GPURasterizer::Draw"
	rasterizerEvents := instance.gpuThread.FindEvents(benchmarking.EventsFilter{Name: &drawStr})
	rasterizerDurations := convertMicrosToMillis(extractDurations(rasterizerEvents))

	type Metric struct {
		Name   string
		Values []float64
	}

	metrics := []Metric{
		{"frame_build_times", frameDurations},
		{"frame_rasterizer_times", rasterizerDurations},
	}
	for _, metric := range metrics {
		fullName := instance.metricName + "_" + metric.Name
		fmt.Printf("%s: %.4g ms\n", fullName, computeAverage(metric.Values))
		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     fullName,
			TestSuite: testSuite,
			Unit:      benchmarking.Unit(benchmarking.Milliseconds),
			Values:    metric.Values,
		})
	}
	fmt.Printf("\n")
}
