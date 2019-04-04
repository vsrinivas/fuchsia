// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains helper functions for process_gfx_trace.go for measuring latency and fps of scenic.

package main

import (
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"

	"fuchsia.googlesource.com/benchmarking"
)

// Compute the FPS and latency within Scenic for |model|, also writing results to
// |testResultsFile| if provided.
func reportScenicFps(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile, allApps bool, appDebugNames []string) {
	fps, fpsPerTimeWindow := calculateFps(model, "gfx", "FramePresented")
	fmt.Printf("%.4g FPS\nFPS per one-second window: %v\n", fps, fpsPerTimeWindow)

	reportFrameStats(model, testSuite, testResultsFile)

	reportAverageEventTimes(model, testSuite, testResultsFile)
	if allApps || (len(appDebugNames) > 0 && appDebugNames[0] != "") {
		reportScenicPerSessionLatencies(model, testSuite, testResultsFile, allApps, appDebugNames)
	}
}

type sessionKey struct {
	debugName string
	id        float64
}

func getEventsPerSession(model benchmarking.Model, eventCat string, eventName string) map[sessionKey][]*benchmarking.Event {
	events := model.FindEvents(
		benchmarking.EventsFilter{Cat: &eventCat, Name: &eventName})
	eventsPerSession := make(map[sessionKey][]*benchmarking.Event)

	for _, event := range events {
		sessionName, ok := event.Args["session_debug_name"].(string)
		sessionID := 0.0
		if ok {
			sessionID, _ = event.Args["session_id"].(float64)
		}

		session := sessionKey{sessionName, sessionID}
		eventsPerSession[session] = append(eventsPerSession[session], event)
	}

	return eventsPerSession
}

// Compute average times for selected events.
func reportAverageEventTimes(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile) {
	fmt.Printf("\n== Average times ==\n")
	type AverageEvent struct {
		IndentLevel int
		Name        string
		Label       string
	}

	// TODO(SCN-1268): The following events are based on PaperRenderer, as we transition
	// to PaperRenderer2 is rolled out they need to be updated to reflect this.

	// TODO(PT-102): Events that appear to happen simultaneously are lumped together in
	// the same trace event as primary and secondary events. Because of this some
	// of the following measurements show as taking 0.001 ms, when they should
	// share their time with the respective paired event.
	// Find a way to deal with this.

	// List the events we want to include in output. Events are described with:
	//   Indentation level in output.
	//   Event name in trace.
	//   Event name in output (blank if same as event name in trace).
	averageEvents := []AverageEvent{
		{0, "RenderFrame", ""},
		{1, "ApplyScheduledSessionUpdates", ""},
		{2, "escher::CommandBuffer::Submit", "CommandBuffer::Submit"},
		{1, "UpdateAndDeliverMetrics", ""},
		{1, "EngineRenderer::RenderLayers", ""},
		{0, "Scenic Compositor", "Escher GPU time/Scenic Compositor"},
		{1, "SSDO acceleration depth pre-pass", ""},
		{1, "SSDO acceleration lookup table generation", ""},
		{1, "Depth pre-pass", ""},
		{1, "Layout transition before SSDO sampling", ""},
		{1, "SSDO sampling", ""},
		{1, "Layout transition before SSDO filtering", ""},
		{1, "SSDO filter pass 1", ""},
		{1, "SSDO filter pass 2", ""},
		{1, "Lighting pass", ""},
		{1, "Transition to presentation layout", ""},
	}

	gfxStr := "gfx"
	for _, e := range averageEvents {
		events := model.FindEvents(benchmarking.EventsFilter{Cat: &gfxStr, Name: &e.Name})
		durations := convertMicrosToMillis(extractDurations(events))
		avgDuration := jsonFloat(benchmarking.AvgDuration(events) / OneMsecInUsecs)
		if e.Label == "" {
			e.Label = e.Name
		}
		fmt.Printf("%-50s %.4g ms\n", strings.Repeat("  ", e.IndentLevel)+e.Label,
			avgDuration)
		if len(durations) == 0 {
			durations = []float64{0}
		}

		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     e.Label,
			TestSuite: testSuite,
			Unit:      benchmarking.Milliseconds,
			Values:    durations,
		})
	}
	renderFrameStr := "RenderFrame"
	scenicCompositorStr := "Scenic Compositor"
	events := model.FindEvents(benchmarking.EventsFilter{Cat: &gfxStr, Name: &renderFrameStr})
	events = append(events, model.FindEvents(benchmarking.EventsFilter{Cat: &gfxStr, Name: &scenicCompositorStr})...)
	sort.Sort(ByStartTime(events))

	fmt.Printf("%-50s %.4g ms\n", "unaccounted (mostly gfx driver)",
		jsonFloat(averageGap(events, gfxStr, renderFrameStr, gfxStr, scenicCompositorStr)/OneMsecInUsecs))
}

// Calculate fps and latency for each session.
func reportScenicPerSessionLatencies(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile, allApps bool, debugNames []string) {
	fmt.Printf("\n=== Scenic Per Session ===\n")
	latenciesPerSession := calculatePerSessionLatency(model)

	essentialLatencies := make(map[string]sessionLatencies)

	for session, latencies := range latenciesPerSession {
		if allApps {
			metricName := session.debugName + "_" + strconv.Itoa(int(session.id))
			essentialLatencies[metricName] = latencies
		} else if listContainsElement(debugNames, session.debugName) {
			metricName := session.debugName
			essentialLatencies[metricName] = latencies
		}
	}

	for metricName, latencies := range essentialLatencies {

		averageLatency := computeAverage(latencies.requestedPresentationTimeToDisplay)
		minLatency := computeMin(latencies.requestedPresentationTimeToDisplay)
		maxLatency := computeMax(latencies.requestedPresentationTimeToDisplay)
		avgScheduleToApply := computeAverage(latencies.scheduleUpdateToApplyUpdate)
		minSchedToApply := computeMin(latencies.scheduleUpdateToApplyUpdate)
		maxSchedToApply := computeMax(latencies.scheduleUpdateToApplyUpdate)
		avgScheduleToDisplay := computeAverage(latencies.applyUpdateToDisplay)
		minSchedToDisplay := computeMin(latencies.applyUpdateToDisplay)
		maxSchedToDisplay := computeMax(latencies.applyUpdateToDisplay)

		fmt.Printf(`%s:
    Latency between requested presentation time and display time:
    	Avg: %.4g ms
    	Min: %.4g ms
    	Max: %.4g ms
    Time from ScheduleUpdate to ApplyUpdate:
    	Avg: %.4g ms
    	Min: %.4g ms
    	Max: %.4g ms
    Time from ApplyUpdate to display:
    	Avg: %.4g ms
    	Min: %.4g ms
    	Max: %.4g ms

`,
			metricName,
			averageLatency,
			minLatency,
			maxLatency,
			avgScheduleToApply,
			minSchedToApply,
			maxSchedToApply,
			avgScheduleToDisplay,
			minSchedToDisplay,
			maxSchedToDisplay)

		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     metricName + "_requested_presentation_time_to_display_latency",
			TestSuite: testSuite,
			Unit:      benchmarking.Milliseconds,
			Values:    latencies.requestedPresentationTimeToDisplay,
		})

		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     metricName + "_schedule_update_to_apply_update_latency",
			TestSuite: testSuite,
			Unit:      benchmarking.Milliseconds,
			Values:    latencies.scheduleUpdateToApplyUpdate,
		})

		testResultsFile.Add(&benchmarking.TestCaseResults{
			Label:     metricName + "_apply_update_to_display_latency",
			TestSuite: testSuite,
			Unit:      benchmarking.Milliseconds,
			Values:    latencies.applyUpdateToDisplay,
		})
	}
}

type sessionLatencies struct {
	requestedPresentationTimeToDisplay []float64
	scheduleUpdateToApplyUpdate        []float64
	applyUpdateToDisplay               []float64
}

// Calculate latency in scenic for all sessions in trace.
func calculatePerSessionLatency(model benchmarking.Model) map[sessionKey]sessionLatencies {
	cat := "gfx"
	scheduleEvents := getEventsPerSession(model, cat, "Session::ScheduleUpdate")
	applyEvents := getEventsPerSession(model, cat, "Session::ApplyScheduledUpdates")
	presentEventName := "DisplaySwapchain::DrawAndPresent() present"
	presentEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &presentEventName})
	startOfGpuWorkEventName := "Scenic Compositor"
	startOfGpuWorkEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &startOfGpuWorkEventName})
	endOfGpuWorkEventName := "escher::CommandBuffer::Retire::callback"
	endOfGpuWorkEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &endOfGpuWorkEventName})

	vsyncName := "VSYNC"
	vsyncEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &vsyncName})
	sort.Sort(ByStartTime(vsyncEvents))
	frameLength := vsyncEvents[1].Start - vsyncEvents[0].Start
	frameLength = convertMicrosToMillis([]float64{frameLength})[0]

	latencyPerSession := make(map[sessionKey]sessionLatencies)
	for session, scheduleEventsOfSession := range scheduleEvents {
		latencyPerSession[session] = calculateSessionLatency(scheduleEventsOfSession, applyEvents[session], presentEvents, startOfGpuWorkEvents, endOfGpuWorkEvents, vsyncEvents, frameLength)
	}

	return latencyPerSession
}

func calculateSessionLatency(scheduleEvents []*benchmarking.Event, applyEvents []*benchmarking.Event, presentEvents []*benchmarking.Event, startOfGpuWorkEvents []*benchmarking.Event, endOfGpuWorkEvents []*benchmarking.Event, vsyncEvents []*benchmarking.Event, frameLengthInMillis float64) sessionLatencies {
	sort.Sort(ByStartTime(scheduleEvents))
	sort.Sort(ByStartTime(applyEvents))

	applyIndex := 0
	presentIndex := 0
	startGpuWorkIndex := 0
	endGpuWorkIndex := 0
	vsyncIndex := 0

	presentToDisplayLatencies := make([]float64, 0, len(applyEvents))
	schedToApplyLatencies := make([]float64, 0, len(applyEvents))
	applyToDisplayLatencies := make([]float64, 0, len(applyEvents))

	requestedTimeString := "requested time"

	for _, event := range scheduleEvents {
		// If requested presentation time is in the past then schedule for now.
		requestedTime, ok := event.Args[requestedTimeString].(float64)
		if !ok {
			panic("No requested time argument found")
		}

		scheduleRequestTime := event.Start
		expectedPresentationTime := math.Max(scheduleRequestTime, requestedTime/1000.0)

		// Find end of first following ApplyScheduledUpdate with same requested presentation time.
		for applyIndex < len(applyEvents) &&
			(applyEvents[applyIndex].Start < scheduleRequestTime ||
				applyEvents[applyIndex].Args[requestedTimeString].(float64) != requestedTime) {
			applyIndex++
		}
		if applyIndex >= len(applyEvents) {
			break
		}
		applyTimeStart := applyEvents[applyIndex].Start
		applyTimeEnd := applyTimeStart + applyEvents[applyIndex].Dur

		// Find end of following GPU upload event.
		for presentIndex < len(presentEvents) &&
			presentEvents[presentIndex].Start < applyTimeEnd {
			presentIndex++
		}
		if presentIndex >= len(presentEvents) {
			break
		}
		presentTimeEnd := presentEvents[presentIndex].Start + presentEvents[presentIndex].Dur

		// Find next start-GPU-work event.
		for startGpuWorkIndex < len(startOfGpuWorkEvents) &&
			startOfGpuWorkEvents[startGpuWorkIndex].Start < presentTimeEnd {
			startGpuWorkIndex++
		}
		if startGpuWorkIndex >= len(startOfGpuWorkEvents) {
			break
		}
		startOfGpuTime := startOfGpuWorkEvents[startGpuWorkIndex].Start

		// Find next GPU-work-done event.
		for endGpuWorkIndex < len(endOfGpuWorkEvents) &&
			endOfGpuWorkEvents[endGpuWorkIndex].Start < startOfGpuTime {
			endGpuWorkIndex++
		}
		if endGpuWorkIndex >= len(endOfGpuWorkEvents) {
			break
		}
		endOfGpuTime := endOfGpuWorkEvents[endGpuWorkIndex].Start

		// Find next VSYNC after GPU finished.
		for vsyncIndex < len(vsyncEvents) &&
			vsyncEvents[vsyncIndex].Start < endOfGpuTime {
			vsyncIndex++
		}
		if vsyncIndex >= len(vsyncEvents) {
			break
		}
		displayTime := vsyncEvents[vsyncIndex].Start

		// Calculate results.
		presentToDisplayLatency := displayTime - expectedPresentationTime
		schedToApplyLatency := applyTimeEnd - scheduleRequestTime
		applyToDisplayLatency := displayTime - applyTimeStart

		presentToDisplayLatencies = append(presentToDisplayLatencies, presentToDisplayLatency)
		schedToApplyLatencies = append(schedToApplyLatencies, schedToApplyLatency)
		applyToDisplayLatencies = append(applyToDisplayLatencies, applyToDisplayLatency)
	}

	presentToDisplayLatencies = convertMicrosToMillis(presentToDisplayLatencies)
	schedToApplyLatencies = convertMicrosToMillis(schedToApplyLatencies)
	applyToDisplayLatencies = convertMicrosToMillis(applyToDisplayLatencies)

	return sessionLatencies{
		presentToDisplayLatencies,
		schedToApplyLatencies,
		applyToDisplayLatencies,
	}
}

func reportFrameStats(model benchmarking.Model, testSuite string, testResultsFile *benchmarking.TestResultsFile) {
	cat := "gfx"
	renderStartedEventName := "Render start"
	renderStartedEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &renderStartedEventName})
	framePresentedEventName := "FramePresented"
	framePresentedEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &framePresentedEventName})
	frameDroppedEventName := "FrameDropped"
	frameDroppedEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &frameDroppedEventName})
	timestampedVsyncEventName := "Display::OnVsync"
	timestampedVsyncEvents := model.FindEvents(benchmarking.EventsFilter{Cat: &cat, Name: &timestampedVsyncEventName})

	sort.Sort(ByStartTime(renderStartedEvents))
	sort.Sort(ByStartTime(framePresentedEvents))
	sort.Sort(ByStartTime(timestampedVsyncEvents))

	framePresentedIndex := 0

	missedPredictions := 0
	const thresholdInNanos = 2000000.0

	frameNumberString := "frame_number"
	expectedPresentationTimeString := "Expected presentation time"
	presentedTimeString := "presentation time"
	timestampString := "Timestamp"

	// Check that events are well formed.
	if len(renderStartedEvents) == 0 {
		panic("No render started events found")
	}
	_, ok := renderStartedEvents[0].Args[frameNumberString].(float64)
	if !ok {
		panic("No frame number argument found for render started event")
	}
	_, ok = renderStartedEvents[0].Args[expectedPresentationTimeString].(float64)
	if !ok {
		panic("No expected presentation time argument found")
	}

	if len(framePresentedEvents) == 0 {
		panic("No frame presented events found")
	}
	_, ok = framePresentedEvents[0].Args[frameNumberString].(float64)
	if !ok {
		panic("No frame number argument found for frame presented event")
	}
	_, ok = framePresentedEvents[0].Args[presentedTimeString].(float64)
	if !ok {
		panic("No time presented argument found")
	}

	if len(timestampedVsyncEvents) == 0 {
		panic("No timestamped vsync events found")
	}
	_, ok = timestampedVsyncEvents[0].Args[timestampString].(float64)
	if !ok {
		panic("No timestamp argument found")
	}

	for _, renderStartEvent := range renderStartedEvents {
		frameNumber := renderStartEvent.Args[frameNumberString].(float64)
		renderStartTime := renderStartEvent.Start

		// Find the corresponding frame presented event.
		for framePresentedIndex < len(framePresentedEvents) &&
			framePresentedEvents[framePresentedIndex].Args[frameNumberString].(float64) < frameNumber {
			framePresentedIndex++
		}

		// If no more presents, we're done.
		if framePresentedIndex >= len(framePresentedEvents) {
			break
		}

		// Check for delayed frames.
		expectedTime := renderStartEvent.Args[expectedPresentationTimeString].(float64)
		presentedTime := framePresentedEvents[framePresentedIndex].Args[presentedTimeString].(float64)
		if presentedTime-expectedTime > thresholdInNanos {
			// Find first vsync after render start.
			vsyncIndex := 0
			for vsyncIndex < len(timestampedVsyncEvents) && timestampedVsyncEvents[vsyncIndex].Start < renderStartTime {
				vsyncIndex++
			}

			// Find vsync actually presented on.
			vsyncCount := 0
			for vsyncIndex < len(timestampedVsyncEvents) && timestampedVsyncEvents[vsyncIndex].Args[timestampString].(float64) != presentedTime {
				vsyncCount++
				vsyncIndex++
			}
			if vsyncIndex > len(timestampedVsyncEvents) {
				break
			}

			missedPredictions += vsyncCount
		}
	}

	framesDrawn := len(framePresentedEvents)
	framesDropped := len(frameDroppedEvents)

	vsyncCount := 0
	firstRenderStartedTime := renderStartedEvents[0].Start
	lastPresentedTime := framePresentedEvents[len(framePresentedEvents)-1].Start
	if len(frameDroppedEvents) > 0 {
		lastPresentedTime = math.Max(lastPresentedTime, frameDroppedEvents[len(frameDroppedEvents)-1].Start)
	}
	for _, vsync := range timestampedVsyncEvents {
		if vsync.Start >= lastPresentedTime {
			break
		}
		if vsync.Start > firstRenderStartedTime {
			vsyncCount++
		}
	}

	emptyVsyncs := vsyncCount - (framesDrawn + framesDropped)
	framesDelayed := missedPredictions - framesDropped

	fmt.Printf(`
Frames drawn: %d
Frames mispredicted: %d
Frames dropped: %d
Frames delayed: %d
Vsync intervals with no render started: %d
`,
		framesDrawn, missedPredictions, framesDropped, framesDelayed, emptyVsyncs)

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "scenic_total_frames_drawn",
		TestSuite: testSuite,
		Unit:      benchmarking.Count,
		Values:    []float64{jsonFloat(float64(framesDrawn))},
	})
	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "scenic_total_mispredicted_frames",
		TestSuite: testSuite,
		Unit:      benchmarking.Count,
		Values:    []float64{jsonFloat(float64(missedPredictions))},
	})
	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "scenic_total_frames_dropped",
		TestSuite: testSuite,
		Unit:      benchmarking.Count,
		Values:    []float64{jsonFloat(float64(framesDropped))},
	})
	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "scenic_total_frames_delayed",
		TestSuite: testSuite,
		Unit:      benchmarking.Count,
		Values:    []float64{jsonFloat(float64(framesDelayed))},
	})

	testResultsFile.Add(&benchmarking.TestCaseResults{
		Label:     "scenic_total_empty_vsync_intervals",
		TestSuite: testSuite,
		Unit:      benchmarking.Count,
		Values:    []float64{jsonFloat(float64(emptyVsyncs))},
	})

}
