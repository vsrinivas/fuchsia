// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmarking

import (
	"fmt"
)

// Extract the list of average cpu percentage usages from the trace model
// under system_metrics category, and write it into testResultsFile.
func ReportCpuMetrics(model Model, testSuite string, testResultsFile *TestResultsFile) {
	fmt.Printf("=== CPU ===\n")
	cpuPercentages := extractCounterValues(
		model, "system_metrics", "cpu_usage", []string{"average_cpu_percentage"})["average_cpu_percentage"]
	fmt.Printf("Average CPU Load: %f\n", computeAverage(cpuPercentages))
	testResultsFile.Add(&TestCaseResults{
		Label:     "CPU Load",
		TestSuite: testSuite,
		Unit:      Unit(Percent),
		Values:    cpuPercentages,
	})
}

// Extract used memory, VMO memory, MMU Overhead memory and IPC memory
// from the trace model under memory_monitor category, and write it into testResultsFile.
func ReportMemoryMetrics(model Model, testSuite string, testResultsFile *TestResultsFile) {
	fmt.Printf("=== Memory ===\n")
	allocatedMemoryValues := extractCounterValues(
		model, "memory_monitor", "allocated", []string{"vmo", "mmu_overhead", "ipc"})
	totalMemory := extractCounterValues(
		model, "memory_monitor", "fixed", []string{"total"})["total"][0]
	freeMemoryValues := extractCounterValues(
		model, "memory_monitor", "free", []string{"free"})["free"]
	usedMemoryValues := make([]float64, 0)
	for _, freeMemory := range freeMemoryValues {
		usedMemoryValues = append(usedMemoryValues, totalMemory-freeMemory)
	}
	type SystemMetric struct {
		Name   string
		Values []float64
	}
	systemMetrics := []SystemMetric{
		{"Total System Memory", usedMemoryValues},
		{"VMO Memory", allocatedMemoryValues["vmo"]},
		{"MMU Overhead Memory", allocatedMemoryValues["mmu_overhead"]},
		{"IPC Memory", allocatedMemoryValues["ipc"]},
	}
	for _, metric := range systemMetrics {
		fmt.Printf("Average %s in bytes: %f\n", metric.Name, computeAverage(metric.Values))
		testResultsFile.Add(&TestCaseResults{
			Label:     metric.Name,
			TestSuite: testSuite,
			Unit:      Unit(Bytes),
			Values:    metric.Values,
		})
	}
}

// Helper function to extract int or float64 values for a particular category,
// name and a list of args from the trace model.
// Returns a map from each arg to the corresponding list of values found.
// All values are cast to float64.
func extractCounterValues(model Model, cat string, name string, args []string) map[string][]float64 {
	events := model.FindEvents(EventsFilter{Name: &name, Cat: &cat})
	if len(events) == 0 {
		panic(fmt.Sprintf("Found 0 events with category=%s and name=%s", cat, name))
	}
	argToValues := map[string][]float64{}
	var value float64
	for _, event := range events {
		for _, arg := range args {
			switch event.Args[arg].(type) {
			case int:
				value = float64(event.Args[arg].(int))
			case float64:
				value = event.Args[arg].(float64)
			}
			argToValues[arg] = append(argToValues[arg], value)
		}
	}
	return argToValues
}

// Compute the average of an array of float64 values.
func computeAverage(array []float64) float64 {
	result := 0.0
	for _, item := range array {
		result += item
	}
	return result / float64(len(array))
}
