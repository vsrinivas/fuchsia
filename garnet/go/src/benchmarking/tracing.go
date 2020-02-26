// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package benchmarking

import (
	"bytes"
	"encoding/json"
	"math"
	"sort"
	"strconv"
)

// Sorting helpers.
type ByStartTime []traceEvent

func (a ByStartTime) Len() int           { return len(a) }
func (a ByStartTime) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByStartTime) Less(i, j int) bool { return a[i].Ts < a[j].Ts }

// Structs for parsing trace files (JSON).

var _ json.Unmarshaler = (*ID)(nil)

// ID accomodates numbers of the form: 7, "43", "0x123".
type ID uint64

func (i *ID) UnmarshalJSON(b []byte) error {
	if quote := []byte{'"'}; bytes.HasPrefix(b, quote) && bytes.HasSuffix(b, quote) {
		b = bytes.TrimPrefix(b, quote)
		b = bytes.TrimSuffix(b, quote)
	}

	if hexPrefix := []byte{'0', 'x'}; bytes.HasPrefix(b, hexPrefix) {
		b = bytes.TrimPrefix(b, hexPrefix)
		n, err := strconv.ParseUint(string(b), 16, 64)
		if err == nil {
			*i = ID(n)
		}
		return err
	}

	n, err := strconv.ParseUint(string(b), 10, 64)
	if err == nil {
		*i = ID(n)
	}
	return err
}

// A struct that represents objects found within the list at the "traceEvents"
// field of the root trace object.  Note that the "Dur" field not actually
// found in the original JSON, it something that we compute later ourselves
// using the timestamps of begin/end events.
//
// Example instance:
// {
//   "args": {
//     "elapsed time since presentation": 1,
//     "frame_number": 919,
//     "presentation time": 96695880,
//     "target time missed by": -3
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
	Id   ID
	Dur  float64
	Args map[string]interface{}
}

// Converts the Id from json.Number to uint64.
func (t traceEvent) ID() uint64 {
	return uint64(t.Id)
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
	CounterEvent  EventType = 4
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

	// The parent event of the event.
	//
	// For durations: The duration event's parent duration, unless the
	// duration event is a top level duration, in which case it will be set to
	// nil.
	// So for example, in:
	//   (======foo==========)
	//   (==bar==) (===baz===)
	// bar will have parent foo, baz will have parent foo, and foo will have
	// parent nil.
	//
	// For flow events: The enclosing duration of the flow event.  Will never be
	// nil.
	//
	// Nil for all other events.
	//
	Parent *Event

	// All children events of the event.
	//
	// For durations: All child durations (durations that have us set as the
	// parent), *and* all flow events that have us as the enclosing duration.
	// So for example, in:
	//   (======foo=======)
	//   (==bar==)(==baz==)
	// foo will have children [bar, baz], bar will have children [], and baz
	// will have children [].
	//
	// For flow events: If the flow event is of flow type "start" or "step", the
	// next flow event in the flow sequence.  [] if the flow event is of flow
	// type "end".
	//
	// [] for all other events.
	//
	Children []*Event
}

// A struct that represents a Thread in the model.
type Thread struct {
	Name   string
	Tid    uint64
	Events []*Event
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

// This is the time duration between the start of the first trace event
// to the end of the last trace event.
// If |m| contains 0 trace events, returns 0.0.
func (m Model) getTotalTraceDurationInMicroseconds() float64 {
	traceStartTime := math.MaxFloat64
	traceEndTime := math.SmallestNonzeroFloat64
	for _, process := range m.Processes {
		for _, thread := range process.Threads {
			for _, event := range thread.Events {
				if event.Type != FlowEvent {
					traceStartTime = math.Min(traceStartTime, event.Start)
					traceEndTime = math.Max(traceEndTime, event.Start+event.Dur)
				}
			}
		}
	}
	return math.Max(0.0, traceEndTime-traceStartTime)
}

func (m Model) getProcessById(pid uint64) *Process {
	for i := range m.Processes {
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
	for i := range p.Threads {
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

// Similar to |combineArgs|, but for applying additional |Args| on a
// |traceEvent| to an existing |Event|.
func combineArgsEventTraceEvent(e *Event, te *traceEvent) {
	if e.Args == nil {
		e.Args = te.Args
	}
	if te.Args == nil {
		return
	}

	for k, v := range te.Args {
		e.Args[k] = v
	}
}

// Structs used to match beginning and end of trace events.

// Used to match sync events.
type pidAndTid struct {
	Pid uint64
	Tid uint64
}

// Used to match async events.
type catAndID struct {
	Cat string
	Id  uint64
}

type FlowId struct {
	Id   uint64
	Cat  string
	Name string
}

func (m *Model) processTraceEvents(traceEvents []traceEvent) {
	// Create synthetic "E" events for all complete events, in order to assist
	// with maintaining a stack.
	extendedEvents := make([]traceEvent, len(traceEvents))
	copy(extendedEvents, traceEvents)
	n := len(traceEvents)
	i := 0
	for i < n {
		event := traceEvents[i]
		if event.Ph == "X" {
			syntheticEnd := event
			syntheticEnd.Ph = "E"
			syntheticEnd.Ts = syntheticEnd.Ts + syntheticEnd.Dur
			syntheticEnd.Dur = -1.0
			extendedEvents = append(extendedEvents, syntheticEnd)
		}
		i++
	}

	sort.Sort(ByStartTime(extendedEvents))

	asyncEvents := make(map[catAndID]traceEvent)
	durationStacks := make(map[pidAndTid][]*Event)
	liveFlowEvents := make(map[FlowId]*Event)

	for _, traceEvent := range extendedEvents {
		pidAndTid := pidAndTid{traceEvent.Pid, traceEvent.Tid}
		thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)

		switch ph := traceEvent.Ph; ph {
		case "X":
			// Complete event.
			durationEvent := Event{DurationEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid,
				traceEvent.Tid, traceEvent.Ts, traceEvent.Dur, traceEvent.ID(), traceEvent.Args, nil, make([]*Event, 0)}
			thread.Events = append(thread.Events, &durationEvent)
			durationStacks[pidAndTid] = append(durationStacks[pidAndTid], thread.Events[len(thread.Events)-1])
			if len(durationStacks[pidAndTid]) > 1 {
				top := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-1]
				topParent := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-2]
				top.Parent = topParent
				topParent.Children = append(topParent.Children, top)
			}
		case "B":
			durationEvent := Event{DurationEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid,
				traceEvent.Tid, traceEvent.Ts, -1.0, traceEvent.ID(), traceEvent.Args, nil, make([]*Event, 0)}
			thread.Events = append(thread.Events, &durationEvent)
			durationStacks[pidAndTid] = append(durationStacks[pidAndTid], thread.Events[len(thread.Events)-1])
			if len(durationStacks[pidAndTid]) > 1 {
				top := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-1]
				topParent := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-2]
				top.Parent = topParent
				topParent.Children = append(topParent.Children, top)
			}
		case "E":
			if len(durationStacks[pidAndTid]) > 0 {
				top := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-1]
				// In the case where the top of the duration durationStacks[pidAndTid] came from a
				// begin event (rather than a complete event), fill in its
				// duration using the current end event.
				if top.Dur == -1.0 {
					top.Dur = traceEvent.Ts - top.Start
				}
				combineArgsEventTraceEvent(top, &traceEvent)
				// Pop the last event from the duration stack.
				durationStacks[pidAndTid] = durationStacks[pidAndTid][:len(durationStacks[pidAndTid])-1]
			}
		case "b":
			// Async begin duration event.
			asyncEvents[catAndID{traceEvent.Cat, traceEvent.ID()}] = traceEvent
		case "e":
			// Async end duration event.
			beginEvent, ok := asyncEvents[catAndID{traceEvent.Cat, traceEvent.ID()}]
			if ok {
				if beginEvent.Cat != traceEvent.Cat {
					panic("Category for begin and end event does not match")
				}
				if beginEvent.ID() != traceEvent.ID() {
					panic("Id for begin and end event does not match")
				}
				asyncEvent := Event{AsyncEvent, beginEvent.Cat,
					beginEvent.Name, beginEvent.Pid, beginEvent.Tid,
					beginEvent.Ts, traceEvent.Ts - beginEvent.Ts, beginEvent.ID(),
					combineArgs(beginEvent, traceEvent), nil, make([]*Event, 0)}
				thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
				thread.Events = append(thread.Events, &asyncEvent)
			}
		case "i":
			// Instant event.
			instantEvent := Event{InstantEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid,
				traceEvent.Tid, traceEvent.Ts, 0, traceEvent.ID(), traceEvent.Args, nil, make([]*Event, 0)}
			thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
			thread.Events = append(thread.Events, &instantEvent)
		case "s":
			flowId := FlowId{traceEvent.ID(), traceEvent.Cat, traceEvent.Name}
			if _, found := liveFlowEvents[flowId]; found {
				// Drop flow begins that already have flow ids in progress.
				continue
			}

			flowEvent := Event{FlowEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid, traceEvent.Tid, traceEvent.Ts, 0, traceEvent.ID(), traceEvent.Args, nil, make([]*Event, 0)}
			thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
			thread.Events = append(thread.Events, &flowEvent)
			liveFlowEvents[flowId] = thread.Events[len(thread.Events)-1]

			if len(durationStacks[pidAndTid]) > 0 {
				top := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-1]
				thread.Events[len(thread.Events)-1].Parent = top
				top.Children = append(top.Children, thread.Events[len(thread.Events)-1])
			}
		case "t":
			flowId := FlowId{traceEvent.ID(), traceEvent.Cat, traceEvent.Name}
			if _, found := liveFlowEvents[flowId]; !found {
				// Drop flow steps that are not in progress.
				continue
			}
			previousFlowEvent := liveFlowEvents[flowId]
			flowEvent := Event{FlowEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid, traceEvent.Tid, traceEvent.Ts, 0, traceEvent.ID(), traceEvent.Args, nil, make([]*Event, 0)}
			thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
			thread.Events = append(thread.Events, &flowEvent)
			liveFlowEvents[flowId] = thread.Events[len(thread.Events)-1]

			previousFlowEvent.Children = append(previousFlowEvent.Children, thread.Events[len(thread.Events)-1])

			if len(durationStacks[pidAndTid]) > 0 {
				top := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-1]
				thread.Events[len(thread.Events)-1].Parent = top
				top.Children = append(top.Children, thread.Events[len(thread.Events)-1])
			}
		case "f":
			flowId := FlowId{traceEvent.ID(), traceEvent.Cat, traceEvent.Name}
			if _, found := liveFlowEvents[flowId]; !found {
				// Drop flow ends that are not in progress.
				continue
			}
			previousFlowEvent := liveFlowEvents[flowId]
			flowEvent := Event{FlowEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid, traceEvent.Tid, traceEvent.Ts, 0, traceEvent.ID(), traceEvent.Args, nil, make([]*Event, 0)}
			thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
			thread.Events = append(thread.Events, &flowEvent)
			previousFlowEvent.Children = append(previousFlowEvent.Children, thread.Events[len(thread.Events)-1])
			if len(durationStacks[pidAndTid]) > 0 {
				top := durationStacks[pidAndTid][len(durationStacks[pidAndTid])-1]
				thread.Events[len(thread.Events)-1].Parent = top
				top.Children = append(top.Children, thread.Events[len(thread.Events)-1])
			}
			delete(liveFlowEvents, flowId)
		case "C":
			// Counter event.
			counterEvent := Event{CounterEvent, traceEvent.Cat, traceEvent.Name, traceEvent.Pid,
				traceEvent.Tid, traceEvent.Ts, 0, traceEvent.ID(), traceEvent.Args, nil, make([]*Event, 0)}
			thread := m.getOrCreateThreadById(traceEvent.Pid, traceEvent.Tid)
			thread.Events = append(thread.Events, &counterEvent)
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
func (t Thread) FindEvents(filter EventsFilter) []*Event {
	var events []*Event
	for _, event := range t.Events {
		if (filter.Cat == nil || event.Cat == *filter.Cat) &&
			(filter.Name == nil || event.Name == *filter.Name) {
			events = append(events, event)
		}
	}
	return events
}

// A method that finds events in the given process, filtered by |filter|.
func (p Process) FindEvents(filter EventsFilter) []*Event {
	var events []*Event
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
func (m Model) FindEvents(filter EventsFilter) []*Event {
	var events []*Event
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
func AvgDuration(events []*Event) float64 {
	totalTime := 0.0
	numEvents := 0.0

	for _, e := range events {
		totalTime += e.Dur
		numEvents += 1
	}
	return totalTime / numEvents
}
