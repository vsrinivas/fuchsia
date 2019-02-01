// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmarking

import (
	"encoding/json"
)

// Structs for parsing trace files (JSON).

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
type traceEvent struct {
	Cat  string
	Name string
	Ph   string
	Pid  uint64
	Tid  uint64
	Ts   float64
	Id   uint64
	Dur  float64
	Args map[string]interface{}
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
type systemTraceEvent struct {
	Name string
	Pid  uint64
	Tid  uint64
	Ph   string
}

// A struct that represents the object found at the "systemTraceEvents" field
// of the root trace object.
type systemTraceEvents struct {
	Type   string
	Events []systemTraceEvent
}

// The root level object found in the trace file.
type trace struct {
	TraceEvents       []traceEvent
	SystemTraceEvents systemTraceEvents
	DisplayTimeUnit   string
}

// Defines the type of Event in the model.
type EventType int

// Listing the possible Event types.
const (
	DurationEvent EventType = 0
	AsyncEvent    EventType = 1
	InstantEvent  EventType = 2
	FlowEvent     EventType = 3
)

// A struct that represents an Event in the model.
type Event struct {
	Type  EventType
	Cat   string
	Name  string
	Pid   uint64
	Tid   uint64
	Start float64
	Dur   float64
	Id    uint64 // Used for async events.
	Args  map[string]interface{}
}

// A struct that represents a Thread in the model.
type Thread struct {
	Name   string
	Tid    uint64
	Events []Event
}

// A struct that represents a Process in the model.
type Process struct {
	Name    string
	Pid     uint64
	Threads []Thread
}

// A struct the represents the root of the model.
type Model struct {
	Processes []Process
}

func (m Model) getProcessById(pid uint64) *Process {
	for i, _ := range m.Processes {
		if m.Processes[i].Pid == pid {
			return &m.Processes[i]
		}
	}
	return nil
}

func (m *Model) getOrCreateProcessById(pid uint64) *Process {
	processPtr := m.getProcessById(pid)
	if processPtr != nil {
		return processPtr
	}
	newProcess := Process{Pid: pid}
	m.Processes = append(m.Processes, newProcess)
	return &m.Processes[len(m.Processes)-1]
}

func (p Process) getThreadById(tid uint64) *Thread {
	for i, _ := range p.Threads {
		if p.Threads[i].Tid == tid {
			return &p.Threads[i]
		}
	}
	return nil
}

func (m *Model) getOrCreateThreadById(pid uint64, tid uint64) *Thread {
	processPtr := m.getOrCreateProcessById(pid)
	threadPtr := processPtr.getThreadById(tid)
	if threadPtr != nil {
		return threadPtr
	}
	var newThread Thread
	newThread.Tid = tid
	processPtr.Threads = append(processPtr.Threads, newThread)
	return &processPtr.Threads[len(processPtr.Threads)-1]
}

// It combines the 'args' of two 'traceEvent's into one 'args'.
// In case an arg key is repeated between the 2 events, 'event2' value is the
// one returned for that key.
func combineArgs(event1 traceEvent, event2 traceEvent) map[string]interface{} {
	if event1.Args == nil {
		return event2.Args
	}
	if event2.Args == nil {
		return event1.Args
	}

	combinedArgs := make(map[string]interface{})
	for k, v := range event1.Args {
		combinedArgs[k] = v
	}
	for k, v := range event2.Args {
		combinedArgs[k] = v
	}
	return combinedArgs
}

// Structs used to match beginning and end of trace events.

// Used to match sync events
type pidAndTid struct {
	Pid uint64
	Tid uint64
}

// Used to match async events
type catAndID struct {
	Cat string
	Id  uint64
}

func (m *Model) processTraceEvents(traceEvents []traceEvent) {
	eventStacks := make(map[pidAndTid][]traceEvent)
	asyncEvents := make(map[catAndID]traceEvent)

	for _, traceEvent := range traceEvents {
		pidAndTid := pidAndTid{traceEvent.Pid, traceEvent.Tid}
		switch ph := traceEvent.Ph; ph {
		case "X":
			// Complete event.
			durationEvent := Event{DurationEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid,
				traceEvent.Tid, traceEvent.Ts, traceEvent.Dur, traceEvent.Id, traceEvent.Args}
			thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
			thread.Events = append(thread.Events, durationEvent)
		case "B":
			// Begin duration event.
			eventStacks[pidAndTid] = append(eventStacks[pidAndTid], traceEvent)
		case "E":
			// End duration event.
			eventStack := eventStacks[pidAndTid]
			if eventStack != nil && len(eventStack) > 0 {
				// Peek at last event
				beginEvent := eventStack[len(eventStack)-1]

				if beginEvent.Cat != traceEvent.Cat || beginEvent.Name != traceEvent.Name {
					// This is possible since events are not necessarily in
					// chronological order; they are grouped by source. So, when
					// processing a new batch of events, it's possible that we
					// get an end event that didn't have a begin event because
					// we started tracing mid-event.
					eventStacks[pidAndTid] = nil
					continue
				}

				// Pop last event from event stack.
				eventStacks[pidAndTid] = eventStack[:len(eventStack)-1]

				durationEvent := Event{DurationEvent, beginEvent.Cat,
					beginEvent.Name, beginEvent.Pid, beginEvent.Tid,
					beginEvent.Ts, traceEvent.Ts - beginEvent.Ts, beginEvent.Id,
					combineArgs(beginEvent, traceEvent)}
				thread := m.getOrCreateThreadById(beginEvent.Pid, beginEvent.Tid)
				thread.Events = append(thread.Events, durationEvent)
			}
		case "b":
			// Async begin duration event
			asyncEvents[catAndID{traceEvent.Cat, traceEvent.Id}] = traceEvent
		case "e":
			// Async end duration event
			beginEvent, ok := asyncEvents[catAndID{traceEvent.Cat, traceEvent.Id}]
			if ok {
				if beginEvent.Cat != traceEvent.Cat {
					panic("Category for begin and end event does not match")
				}
				if beginEvent.Id != traceEvent.Id {
					panic("Id for begin and end event does not match")
				}
				asyncEvent := Event{AsyncEvent, beginEvent.Cat,
					beginEvent.Name, beginEvent.Pid, beginEvent.Tid,
					beginEvent.Ts, traceEvent.Ts - beginEvent.Ts, beginEvent.Id,
					combineArgs(beginEvent, traceEvent)}
				thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
				thread.Events = append(thread.Events, asyncEvent)
			}
		case "i":
			// Instant event
			instantEvent := Event{InstantEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid,
				traceEvent.Tid, traceEvent.Ts, 0, traceEvent.Id, traceEvent.Args}
			thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
			thread.Events = append(thread.Events, instantEvent)
		}
	}
}

func (m *Model) processSystemEvents(systemEvents []systemTraceEvent) {
	for _, systemEvent := range systemEvents {
		switch ph := systemEvent.Ph; ph {
		case "p":
			// Process declaration.
			process := m.getOrCreateProcessById(systemEvent.Pid)
			process.Name = systemEvent.Name
		case "t":
			// Thread declaration.
			thread := m.getOrCreateThreadById(systemEvent.Pid, systemEvent.Tid)
			thread.Name = systemEvent.Name
		}
	}
}

func computeModel(trace trace) (model Model) {
	model.processTraceEvents(trace.TraceEvents)
	model.processSystemEvents(trace.SystemTraceEvents.Events)
	return model
}

// Reads a JSON trace and returns a trace model.
func ReadTrace(data []byte) (Model, error) {
	// Parsing input.
	var trace trace
	var err = json.Unmarshal([]byte(data), &trace)
	return computeModel(trace), err
}

// A struct that is used by the FindEvents methods to filter events.
// This struct is using pointers to allow detecting null (unset) values.
// If a field is not set, then it is not used when filtering.
type EventsFilter struct {
	Cat  *string
	Name *string
	Pid  *uint64
	Tid  *uint64
}

// A method that finds events in the given thread, filtered by |filter|.
func (t Thread) FindEvents(filter EventsFilter) []Event {
	var events []Event
	for _, event := range t.Events {
		if (filter.Cat == nil || event.Cat == *filter.Cat) &&
			(filter.Name == nil || event.Name == *filter.Name) {
			events = append(events, event)
		}
	}
	return events
}

// A method that finds events in the given process, filtered by |filter|.
func (p Process) FindEvents(filter EventsFilter) []Event {
	var events []Event
	if filter.Tid != nil {
		threadPtr := p.getThreadById(*filter.Tid)
		if threadPtr != nil {
			events = append(events, threadPtr.FindEvents(filter)...)
		}
		return events
	}
	for _, thread := range p.Threads {
		events = append(events, thread.FindEvents(filter)...)
	}
	return events
}

// A method that finds events in the given model, filtered by |filter|.
func (m Model) FindEvents(filter EventsFilter) []Event {
	var events []Event
	if filter.Pid != nil {
		processPtr := m.getProcessById(*filter.Pid)
		if processPtr != nil {
			events = append(events, processPtr.FindEvents(filter)...)
		}
		return events
	}

	for _, process := range m.Processes {
		events = append(events, process.FindEvents(filter)...)
	}
	return events
}

// For the given set of |events|, find the average duration of all instances of
// events with matching |cat| and |name|.
func AvgDuration(events []Event) float64 {
	totalTime := 0.0
	numEvents := 0.0

	for _, e := range events {
		totalTime += e.Dur
		numEvents += 1
	}
	return totalTime / numEvents
}
