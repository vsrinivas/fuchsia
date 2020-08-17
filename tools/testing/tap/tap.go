// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tap

import (
	"bytes"
	"fmt"
	"io"
	"strings"
)

// Type describes the type of TAP Object returned by the Parser.
type Type int

const (
	// VersionType is the type returned by a Version Object.
	VersionType Type = iota

	// PlanType is the type returned by a Plan Object.
	PlanType

	// TestLineType is the type returned by a TestLine.
	TestLineType
)

// Object is a TAP element such as a test line, plan, version header, or Yaml block.
type Object interface {
	Type() Type
}

// Document represents a complete TAP document.
type Document struct {
	Version   Version
	Plan      Plan
	TestLines []TestLine
}

// WriteTo writes this document to the given Writer as a formatted TAP output stream.
func (d *Document) WriteTo(w io.Writer) (int64, error) {
	n, err := w.Write([]byte(d.format()))
	return int64(n), err
}

// Format renders this document as though it were a TAP output stream.
func (d *Document) format() string {
	output := new(bytes.Buffer)
	output.WriteString(fmt.Sprintf("TAP version %d\n", d.Version))
	output.WriteString(fmt.Sprintf("%d..%d\n", d.Plan.Start, d.Plan.End))

	for _, line := range d.TestLines {
		var parts []string
		ok := "ok"
		if !line.Ok {
			ok = "not ok"
		}
		parts = append(parts, ok)

		if line.Count != 0 {
			parts = append(parts, fmt.Sprintf("%d", line.Count))
		}

		if line.Description != "" {
			parts = append(parts, line.Description)
		}

		switch line.Directive {
		case Todo:
			parts = append(parts, "# TODO", line.Explanation)
		case Skip:
			parts = append(parts, "# SKIP", line.Explanation)
		}

		output.WriteString(strings.Join(parts, " ") + "\n")
	}

	return output.String()
}

// Version represents a TAP version line.
type Version int

// Type implements Object.
func (v Version) Type() Type {
	return VersionType
}

func (v Version) String() string {
	return fmt.Sprintf("TAP version %d", v)
}

// Plan represents a TAP plan line.
type Plan struct {
	Start int
	End   int
}

// Type implements Object.
func (p Plan) Type() Type {
	return PlanType
}

func (p Plan) String() string {
	return fmt.Sprintf("%d..%d", p.Start, p.End)
}

// Directive represents a TAP directive (TODO|SKIP|<none>)
type Directive int

// Valid Tap directives.
const (
	None Directive = iota
	Todo
	Skip
)

// TestLine represents a TAP test line beginning with "ok" or "not ok".
type TestLine struct {
	Ok          bool
	Count       int
	Description string
	Directive   Directive
	Explanation string
	Diagnostic  string
	YAML        string
}

// Type implements Object.
func (t TestLine) Type() Type {
	return TestLineType
}
