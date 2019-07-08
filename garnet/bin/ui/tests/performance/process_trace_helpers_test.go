package main

import (
	"math"
	"testing"

	"fuchsia.googlesource.com/benchmarking"
)

func almostEqual(a float64, b float64) bool {
	return math.Abs(a-b) < 1e-6
}

func TestCalculateFpsForEvents(t *testing.T) {
	events := []*benchmarking.Event{
		{
			Start: 3.4 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 3.6 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 3.9 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 4.35 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 4.5 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 4.9 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 5.4 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 7.2 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 7.5 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 10.5 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 10.6 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
		{
			Start: 10.7 * OneSecInUsecs,
			Dur:   0.01 * OneSecInUsecs,
		},
	}
	expectedFps := 12.0 / 7.3
	fps := calculateFpsForEvents(events)
	if !almostEqual(fps, expectedFps) {
		t.Errorf("Calculated incorrect fps: got %f, expected %f", fps, expectedFps)
	}
}
