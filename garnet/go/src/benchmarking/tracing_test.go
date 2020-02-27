// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmarking

import (
	"math"
	"reflect"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func getTestTrace() []byte {
	testTrace := []byte(`
		{
		  "displayTimeUnit": "ns",
		  "traceEvents": [{
			  "cat": "async",
			  "name": "ReadWrite",
			  "ts": 687503138,
			  "id": 43,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "b"
			}, {
			  "cat": "input",
			  "name": "Read",
			  "ts": 697503138.9531089,
			  "pid": 7009,
			  "tid": 7021,
			  "ph": "B"
			}, {
			  "cat": "input",
			  "name": "ReadWriteFlow",
			  "ts": 697503139.9531089,
			  "pid": 7009,
			  "tid": 7021,
			  "ph": "s",
			  "id": 0
			}, {
			  "cat": "input",
			  "name": "Read",
			  "ts": 697503461.7395687,
			  "pid": 7009,
			  "tid": 7021,
			  "ph": "E"
			}, {
			  "cat": "io",
			  "name": "Write",
			  "ts": 697778328.2160872,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "B"
			}, {
			  "cat": "input",
			  "name": "ReadWriteFlow",
			  "ts": 697778329.2160872,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "f",
			  "id": 0
			}, {
			  "cat": "io",
			  "name": "Write",
			  "ts": 697778596.5994568,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "E"
			}, {
			  "cat": "io",
			  "name": "Read",
			  "ts": 697868185.3588456,
			  "pid": 7010,
			  "tid": 7023,
			  "ph": "B"
			}, {
			  "cat": "io",
			  "name": "Read",
			  "ts": 697868571.6018075,
			  "pid": 7010,
			  "tid": 7023,
			  "ph": "E"
			}, {
			  "cat": "async",
			  "name": "ReadWrite",
			  "ts": 687503138,
			  "id": 43,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "e"
			}, {
			  "name": "log",
			  "ph": "i",
			  "ts": 7055567057.312,
			  "pid": 5945,
			  "tid": 5962,
			  "s": "g",
			  "args": {
				"message": "[INFO:trace_manager.cc(66)] Stopping trace"
			  }
			}, {
			  "cat": "system_metrics",
			  "name": "cpu_usage",
			  "ts": 35241122.375,
			  "pid": 9234,
			  "tid": 5678,
				"ph": "C",
				"args": {"average_cpu_percentage": 0.89349317793, "max_cpu_usage": 0.1234}
			}
			],
		  "systemTraceEvents": {
			"type": "fuchsia",
			"events": [{
			  "ph": "p",
			  "pid": 7009,
			  "name": "root_presenter"
			}, {
			  "ph": "t",
			  "pid": 7009,
			  "tid": 7022,
			  "name": "initial-thread"
			}]
		  }
		}`)
	return testTrace
}

// The same value as |getTestTrace|, only without flow events.
func getTestTraceNoFlows() []byte {
	testTrace := []byte(`
		{
		  "displayTimeUnit": "ns",
		  "traceEvents": [{
			  "cat": "async",
			  "name": "ReadWrite",
			  "ts": 687503138,
			  "id": 43,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "b"
			}, {
			  "cat": "input",
			  "name": "Read",
			  "ts": 697503138.9531089,
			  "pid": 7009,
			  "tid": 7021,
			  "ph": "B"
			}, {
			  "cat": "input",
			  "name": "Read",
			  "ts": 697503461.7395687,
			  "pid": 7009,
			  "tid": 7021,
			  "ph": "E"
			}, {
			  "cat": "io",
			  "name": "Write",
			  "ts": 697778328.2160872,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "B"
			}, {
			  "cat": "io",
			  "name": "Write",
			  "ts": 697778596.5994568,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "E"
			}, {
			  "cat": "io",
			  "name": "Read",
			  "ts": 697868185.3588456,
			  "pid": 7010,
			  "tid": 7023,
			  "ph": "B"
			}, {
			  "cat": "io",
			  "name": "Read",
			  "ts": 697868571.6018075,
			  "pid": 7010,
			  "tid": 7023,
			  "ph": "E"
			}, {
			  "cat": "async",
			  "name": "ReadWrite",
			  "ts": 687503138,
			  "id": 43,
			  "pid": 7009,
			  "tid": 7022,
			  "ph": "e"
			}, {
			  "name": "log",
			  "ph": "i",
			  "ts": 7055567057.312,
			  "pid": 5945,
			  "tid": 5962,
			  "s": "g",
			  "args": {
				"message": "[INFO:trace_manager.cc(66)] Stopping trace"
			  }
			}, {
			  "cat": "system_metrics",
			  "name": "cpu_usage",
			  "ts": 35241122.375,
			  "pid": 9234,
			  "tid": 5678,
				"ph": "C",
				"args": {"average_cpu_percentage": 0.89349317793, "max_cpu_usage": 0.1234}
			}
			],
		  "systemTraceEvents": {
			"type": "fuchsia",
			"events": [{
			  "ph": "p",
			  "pid": 7009,
			  "name": "root_presenter"
			}, {
			  "ph": "t",
			  "pid": 7009,
			  "tid": 7022,
			  "name": "initial-thread"
			}]
		  }
		}`)
	return testTrace
}

func TestReadTrace(t *testing.T) {
	expectedModel := Model{
		Processes: []Process{
			{Name: "", Pid: 9234, Threads: []Thread{
				{Name: "", Tid: 5678, Events: []*Event{
					{Type: 4, Cat: "system_metrics", Name: "cpu_usage", Pid: 9234, Tid: 5678, Start: 3.5241122375e+07, Dur: 0, Id: 0, Args: map[string]interface{}{"average_cpu_percentage": 0.89349317793, "max_cpu_usage": 0.1234}, Parent: nil, Children: make([]*Event, 0)},
				}},
			}},
			{Name: "root_presenter", Pid: 7009, Threads: []Thread{
				{Name: "initial-thread", Tid: 7022, Events: []*Event{
					{Type: 1, Cat: "async", Name: "ReadWrite", Pid: 7009, Tid: 7022, Start: 6.87503138e+08, Dur: 0, Id: 43, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
					{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
					{Type: 3, Cat: "input", Name: "ReadWriteFlow", Pid: 7009, Tid: 7022, Start: 6.977783292160872e+08, Dur: 0.0, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
				}},
				{Name: "", Tid: 7021, Events: []*Event{
					{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
					{Type: 3, Cat: "input", Name: "ReadWriteFlow", Pid: 7009, Tid: 7021, Start: 6.975031399531089e+08, Dur: 0.0, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
				}},
			}},
			{Name: "", Pid: 7010, Threads: []Thread{
				{Name: "", Tid: 7023, Events: []*Event{
					{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
				}},
			}},
			{Name: "", Pid: 5945, Threads: []Thread{
				{Name: "", Tid: 5962, Events: []*Event{
					{Type: 2, Cat: "", Name: "log", Pid: 5945, Tid: 5962, Start: 7.055567057312e+09, Dur: 0, Id: 0, Args: map[string]interface{}{"message": "[INFO:trace_manager.cc(66)] Stopping trace"}, Parent: nil, Children: make([]*Event, 0)},
				}},
			}},
		},
	}

	p7009 := expectedModel.Processes[1]
	t7022 := p7009.Threads[0]
	t7021 := p7009.Threads[1]

	writeDurationEvent := t7022.Events[1]
	readWriteFlowEvent7022 := t7022.Events[2]
	writeDurationEvent.Children = append(writeDurationEvent.Children, readWriteFlowEvent7022)
	readWriteFlowEvent7022.Parent = writeDurationEvent

	readDurationEvent := t7021.Events[0]
	readWriteFlowEvent7021 := t7021.Events[1]
	readDurationEvent.Children = append(readDurationEvent.Children, readWriteFlowEvent7021)
	readWriteFlowEvent7021.Parent = readDurationEvent

	readWriteFlowEvent7021.Children = append(readWriteFlowEvent7021.Children, readWriteFlowEvent7022)

	model, err := ReadTrace(getTestTrace())

	if err != nil {
		t.Fatalf("Processing the trace produced an error: %#v\n", err)
	}
	if !reflect.DeepEqual(expectedModel, model) {
		t.Error("Generated model and expected model are different\n")
	}
}

func compareEvents(t *testing.T, description string, expectedEvents []*Event, events []*Event) {
	if diff := cmp.Diff(expectedEvents, events); diff != "" {
		t.Errorf("%s: (-want +got)\n%s", description, diff)
	}
}

func TestGetTotalTraceDurationInMicroseconds(t *testing.T) {
	model := Model{
		Processes: []Process{
			{Name: "", Pid: 9234, Threads: []Thread{
				{Name: "", Tid: 5678, Events: []*Event{
					{Type: 4, Cat: "system_metrics", Name: "cpu_usage", Pid: 9234, Tid: 5678, Start: 3.6000000e+07, Dur: 0, Id: 0, Args: map[string]interface{}{"average_cpu_percentage": 0.89349317793, "max_cpu_usage": 0.1234}, Parent: nil, Children: make([]*Event, 0)},
				}},
			}},
			{Name: "root_presenter", Pid: 7009, Threads: []Thread{
				{Name: "initial-thread", Tid: 7022, Events: []*Event{
					{Type: 1, Cat: "async", Name: "ReadWrite", Pid: 7009, Tid: 7022, Start: 2.0000000e+07, Dur: 0, Id: 43, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
					{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 1.0000000e+07, Dur: 0.6000000e+07, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
					{Type: 3, Cat: "input", Name: "ReadWriteFlow", Pid: 7009, Tid: 7022, Start: 0.0050000e+07, Dur: 6.0000000e+07, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
				}},
			}},
		},
	}
	duration := model.getTotalTraceDurationInMicroseconds()
	expectedDuration := 2.6000000e+07
	if duration != expectedDuration {
		t.Errorf("Total trace duration calculated (%f) and expected duration (%f) are different.",
			duration, expectedDuration)
	}
}

func TestFindEvents(t *testing.T) {
	model, _ := ReadTrace(getTestTraceNoFlows())

	// Find events by Name
	name := "Read"
	expectedEvents := []*Event{
		{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	events := model.FindEvents(EventsFilter{Name: &name})
	compareEvents(t, "Find events by Name", expectedEvents, events)

	// Find events by Category
	cat := "io"
	expectedEvents = []*Event{
		{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	events = model.FindEvents(EventsFilter{Cat: &cat})
	compareEvents(t, "Find events by Category", expectedEvents, events)

	// Find events by Process
	pid := uint64(7009)
	expectedEvents = []*Event{
		{Type: 1, Cat: "async", Name: "ReadWrite", Pid: 7009, Tid: 7022, Start: 6.87503138e+08, Dur: 0, Id: 43, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	events = model.FindEvents(EventsFilter{Pid: &pid})
	compareEvents(t, "Find events by Process", expectedEvents, events)

	// Find events by Thread
	tid := uint64(7022)
	expectedEvents = []*Event{
		{Type: 1, Cat: "async", Name: "ReadWrite", Pid: 7009, Tid: 7022, Start: 6.87503138e+08, Dur: 0, Id: 43, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	events = model.FindEvents(EventsFilter{Tid: &tid})
	compareEvents(t, "Find events by Thread", expectedEvents, events)

	// Find events by Name and Category
	expectedEvents = []*Event{
		{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	events = model.FindEvents(EventsFilter{Name: &name, Cat: &cat})
	compareEvents(t, "Find events by Name and Category", expectedEvents, events)

	cat = "system_metrics"
	name = "cpu_usage"
	expectedEvents = []*Event{
		{Type: 4, Cat: "system_metrics", Name: "cpu_usage", Pid: 9234, Tid: 5678, Start: 3.5241122375e+07, Dur: 0, Id: 0, Args: map[string]interface{}{"average_cpu_percentage": 0.89349317793, "max_cpu_usage": 0.1234}, Parent: nil, Children: make([]*Event, 0)}}
	events = model.FindEvents(EventsFilter{Name: &name, Cat: &cat})
	compareEvents(t, "Find events by Name and Category", expectedEvents, events)
}

func compareAvgDurations(t *testing.T, listSize int, expected float64, actual float64) {
	if expected != actual {
		t.Errorf("Expected average duration of %d events is: %v, actual is: %v\n", listSize, expected, actual)
	}
}

func TestAvgDuration(t *testing.T) {
	// Average of Zero events
	eventList := make([]*Event, 0)
	avg := AvgDuration(eventList)
	if !math.IsNaN(avg) {
		t.Errorf("Expected average duration of Zero events is: NaN, actual is: %v\n", avg)
	}

	// Average of One events.
	eventList = []*Event{
		{Type: 0, Cat: "io", Name: "Write", Pid: 7009, Tid: 7022, Start: 6.977783282160872e+08, Dur: 268.38336956501007, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	avg = AvgDuration(eventList)
	compareAvgDurations(t, len(eventList), eventList[0].Dur, avg)

	// Average of Two events.
	eventList = []*Event{
		{Type: 0, Cat: "input", Name: "Read", Pid: 7009, Tid: 7021, Start: 6.975031389531089e+08, Dur: 322.78645980358124, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 0, Cat: "io", Name: "Read", Pid: 7010, Tid: 7023, Start: 6.978681853588456e+08, Dur: 386.2429618835449, Id: 0, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	avg = AvgDuration(eventList)
	compareAvgDurations(t, len(eventList), (eventList[0].Dur+eventList[1].Dur)/2.0, avg)
}

func TestTraceEventId(t *testing.T) {
	testTrace := []byte(`
	{
		"displayTimeUnit": "ns",
		"traceEvents": [
			{ "cat": "a", "name": "E1", "ts": 10, "id": 7, "pid": 7009, "tid": 7022, "ph": "b"},
			{ "cat": "a", "name": "E1", "ts": 11, "id": 7, "pid": 7009, "tid": 7022, "ph": "e"},
			{ "cat": "a", "name": "E2", "ts": 10, "id": "44", "pid": 7009, "tid": 7022, "ph": "b"},
			{ "cat": "a", "name": "E2", "ts": 12, "id": "44", "pid": 7009, "tid": 7022, "ph": "e"},
			{ "cat": "a", "name": "E3", "ts": 10, "id": "0x123", "pid": 7009, "tid": 7022, "ph": "b"},
			{ "cat": "a", "name": "E3", "ts": 13, "id": "0x123", "pid": 7009, "tid": 7022, "ph": "e"}
		]
	}`)

	model, _ := ReadTrace(testTrace)

	// Match Events by Id of type num
	cat := "a"
	events := model.FindEvents(EventsFilter{Cat: &cat})
	expectedEvents := []*Event{
		{Type: 1, Cat: "a", Name: "E1", Pid: 7009, Tid: 7022, Start: 10, Dur: 1, Id: 7, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 1, Cat: "a", Name: "E2", Pid: 7009, Tid: 7022, Start: 10, Dur: 2, Id: 44, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)},
		{Type: 1, Cat: "a", Name: "E3", Pid: 7009, Tid: 7022, Start: 10, Dur: 3, Id: 291, Args: map[string]interface{}(nil), Parent: nil, Children: make([]*Event, 0)}}
	compareEvents(t, "Match Events by Id of type num", expectedEvents, events)
}
