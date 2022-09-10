// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// Implements productSizeCheckerOutputModules.
type mockProductSizeCheckerOutputModules struct {
	productSizeCheckerOutput []build.ProductSizeCheckerOutput
}

func (m mockProductSizeCheckerOutputModules) BuildDir() string {
	return "BUILD_DIR"
}

func (m mockProductSizeCheckerOutputModules) ProductSizeCheckerOutput() []build.ProductSizeCheckerOutput {
	return m.productSizeCheckerOutput
}

func TestProductSizeCheckerOutputUploads(t *testing.T) {
	m := &mockProductSizeCheckerOutputModules{
		productSizeCheckerOutput: []build.ProductSizeCheckerOutput{
			{
				Visualization: "obj/build/viz",
				SizeBreakdown: "A/B/C/D",
			},
		},
	}
	expected := []Upload{
		{
			Source:      "BUILD_DIR/obj/build/viz",
			Destination: "namespace/visualization",
			Compress:    true,
		},
		{
			Source:      "BUILD_DIR/A/B/C/D",
			Destination: "namespace/size_breakdown.txt",
		},
	}
	actual, err := productSizeCheckerOutputUploads(m, "namespace")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !reflect.DeepEqual(actual, expected) {
		t.Fatalf("unexpected product size checker uploads:\nexpected: %v\nactual: %v\n", expected, actual)
	}
}

func TestTooManyProductSizeCheckerOutputs(t *testing.T) {
	m := &mockProductSizeCheckerOutputModules{
		productSizeCheckerOutput: []build.ProductSizeCheckerOutput{
			{
				Visualization: "obj/build/viz",
				SizeBreakdown: "A/B/C/D",
			},
			{
				Visualization: "viz",
				SizeBreakdown: "x",
			},
		},
	}
	expected_error_message := "Expected 0 or 1 ProductSizeCheckerOutputs, found 2"
	_, err := productSizeCheckerOutputUploads(m, "namespace")
	if err == nil || err.Error() != expected_error_message {
		t.Fatalf("unexpected product size checker error returned:\nexpected: %v\nactual: %v\n", expected_error_message, err)
	}
}
