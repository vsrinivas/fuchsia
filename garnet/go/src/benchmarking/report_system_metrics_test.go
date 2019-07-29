// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmarking

import (
	"reflect"
	"testing"
)

func TestReportCpuMetrics(t *testing.T) {
	var file TestResultsFile
	results := &TestCaseResults{
		Label:     "CPU Load",
		TestSuite: "test suite",
		Unit:      Unit(Percent),
		Values:    []float64{0.123456789, 0.23349317793},
	}
	expectedFile := TestResultsFile{results}
	model := Model{
		Processes: []Process{
			{
				Name: "",
				Pid:  9234,
				Threads: []Thread{
					{
						Name: "",
						Tid:  5678,
						Events: []*Event{
							{
								Type:  4,
								Cat:   "system_metrics",
								Name:  "cpu_usage",
								Pid:   9234,
								Tid:   5678,
								Start: 3.000000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"average_cpu_percentage": 0.123456789,
									"max_cpu_usage":          0.8765,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
							{
								Type:  4,
								Cat:   "system_metrics",
								Name:  "cpu_usage",
								Pid:   9234,
								Tid:   5678,
								Start: 1.000000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"average_cpu_percentage": 0.23349317793,
									"max_cpu_usage":          0.6543,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
						},
					},
				},
			},
		},
	}
	ReportCpuMetrics(model, "test suite", &file)
	if !reflect.DeepEqual(expectedFile, file) {
		t.Error("Expected and actual TestResultFile did not match\n")
	}
}

func TestReportMemoryMetrics(t *testing.T) {
	var file TestResultsFile
	result1 := &TestCaseResults{
		Label:     "Total System Memory",
		TestSuite: "test suite",
		Unit:      Unit(Bytes),
		Values:    []float64{940612736, 990612736},
	}
	result2 := &TestCaseResults{
		Label:     "VMO Memory",
		TestSuite: "test suite",
		Unit:      Unit(Bytes),
		Values:    []float64{781942784, 781942785},
	}
	result3 := &TestCaseResults{
		Label:     "MMU Overhead Memory",
		TestSuite: "test suite",
		Unit:      Unit(Bytes),
		Values:    []float64{77529088, 77529089},
	}
	result4 := &TestCaseResults{
		Label:     "IPC Memory",
		TestSuite: "test suite",
		Unit:      Unit(Bytes),
		Values:    []float64{49152, 49152},
	}
	expectedFile := TestResultsFile{result1, result2, result3, result4}
	model := Model{
		Processes: []Process{
			{
				Name: "",
				Pid:  17783,
				Threads: []Thread{
					{
						Name: "",
						Tid:  17795,
						Events: []*Event{
							{
								Type:  4,
								Cat:   "memory_monitor",
								Name:  "fixed",
								Pid:   17783,
								Tid:   17795,
								Start: 1.000000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"total":      1610612736,
									"wired":      60354560,
									"total_heap": 15183872,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
							{
								Type:  4,
								Cat:   "memory_monitor",
								Name:  "allocated",
								Pid:   17783,
								Tid:   17795,
								Start: 1.500000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"vmo":          781942784,
									"mmu_overhead": 77529088,
									"ipc":          49152,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
							{
								Type:  4,
								Cat:   "memory_monitor",
								Name:  "allocated",
								Pid:   17783,
								Tid:   17795,
								Start: 1.900000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"vmo":          781942785,
									"mmu_overhead": 77529089,
									"ipc":          49152,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
							{
								Type:  4,
								Cat:   "memory_monitor",
								Name:  "free",
								Pid:   17783,
								Tid:   17795,
								Start: 2.500000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"free":      670000000,
									"free_heap": 460000,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
							{
								Type:  4,
								Cat:   "memory_monitor",
								Name:  "free",
								Pid:   17783,
								Tid:   17795,
								Start: 1.800000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"free":      620000000,
									"free_heap": 430000,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
						},
					},
				},
			},
		},
	}

	ReportMemoryMetrics(model, "test suite", &file)
	if !reflect.DeepEqual(expectedFile, file) {
		t.Error("Expected and actual TestResultFile did not match\n")
	}
}

func TestReportTemperatureMetrics(t *testing.T) {
	var file TestResultsFile
	results := &TestCaseResults{
		Label:     "Device temperature",
		TestSuite: "test suite",
		Unit:      Unit(Count),
		Values:    []float64{40, 50},
	}
	expectedFile := TestResultsFile{results}
	model := Model{
		Processes: []Process{
			{
				Name: "",
				Pid:  4567,
				Threads: []Thread{
					{
						Name: "",
						Tid:  1239,
						Events: []*Event{
							{
								Type:  4,
								Cat:   "system_metrics",
								Name:  "temperature",
								Pid:   4567,
								Tid:   1239,
								Start: 14.000000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"temperature": 40,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
							{
								Type:  4,
								Cat:   "system_metrics",
								Name:  "temperature",
								Pid:   4567,
								Tid:   1239,
								Start: 3.000000000e+06,
								Dur:   0,
								Id:    0,
								Args: map[string]interface{}{
									"temperature": 50,
								},
								Parent:   nil,
								Children: make([]*Event, 0),
							},
						},
					},
				},
			},
		},
	}
	ReportTemperatureMetrics(model, "test suite", &file)
	if !reflect.DeepEqual(expectedFile, file) {
		t.Error("Expected and actual TestResultFile did not match\n")
	}
}
