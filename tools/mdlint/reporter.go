// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"sort"
	"strings"
)

// A Reporter provides utilities to aggregate warning messages attached to
// specific tokens, and to pretty print these aggregates messages.
type Reporter struct {
	messages sortableMessages

	// JSONOutput enables JSON output, instead of the pretty printed human
	// readable output.
	JSONOutput bool
}

type message struct {
	tok     token
	content string
}

type sortableMessages []message

var _ sort.Interface = (sortableMessages)(nil)

func (s sortableMessages) Len() int {
	return len(s)
}

func (s sortableMessages) Less(i, j int) bool {
	if byFilename := strings.Compare(s[i].tok.doc.filename, s[j].tok.doc.filename); byFilename < 0 {
		return true
	} else if byFilename > 0 {
		return false
	}

	if byLnNo := s[i].tok.ln - s[j].tok.ln; byLnNo < 0 {
		return true
	} else if byLnNo > 0 {
		return false
	}

	if byColNo := s[i].tok.col - s[j].tok.col; byColNo < 0 {
		return true
	} else if byColNo > 0 {
		return false
	}

	byContent := strings.Compare(s[i].content, s[j].content)
	return byContent < 0
}

func (s sortableMessages) Swap(i, j int) {
	s[i], s[j] = s[j], s[i]
}

// Warnf formats and adds warning message using the format specifier.
func (r *Reporter) Warnf(tok token, format string, a ...interface{}) {
	r.messages = append(r.messages, message{
		tok:     tok,
		content: fmt.Sprintf(format, a...),
	})
}

// HasMessages indicates whether any message was added to this reporter.
func (r *Reporter) HasMessages() bool {
	return len(r.messages) != 0
}

// findingJSON captures the information needed to emit a `message Comment` as
// specified in
// https://chromium.googlesource.com/infra/infra/+/refs/heads/master/go/src/infra/tricium/api/v1/data.proto
type findingJSON struct {
	Category  string `json:"category"`
	Message   string `json:"message"`
	Path      string `json:"path"`
	StartLine int    `json:"start_line"`
	StartChar int    `json:"start_char"`
	EndLine   int    `json:"end_line"`
	EndChar   int    `json:"end_char"`
}

// messagesToFindingsJSON converts the reporter's messages to `findingJSON`.
// This method is used to test the internals of the reporter, but should not be
// used otherwise.
func (r *Reporter) messagesToFindingsJSON() []findingJSON {
	sort.Sort(r.messages)
	var findings []findingJSON
	for _, msg := range r.messages {
		numLines, numCharsOnLastLine := numLinesAndCharsOnLastLine(msg.tok)
		findings = append(findings, findingJSON{
			Category:  "mdlint/general",
			Message:   msg.content,
			Path:      msg.tok.doc.filename,
			StartLine: msg.tok.ln,
			StartChar: msg.tok.col - 1,
			EndLine:   msg.tok.ln + numLines,
			EndChar:   numCharsOnLastLine,
		})
	}
	return findings
}

func numLinesAndCharsOnLastLine(tok token) (int, int) {
	var (
		numLines           int
		numCharsOnLastLine = tok.col - 1
	)
	for _, r := range tok.content {
		if r == '\n' {
			numLines++
			numCharsOnLastLine = 0
		} else {
			numCharsOnLastLine++
		}
	}
	return numLines, numCharsOnLastLine
}

func (r *Reporter) printAsJSON(writer io.Writer) error {
	data, err := json.Marshal(r.messagesToFindingsJSON())
	if err != nil {
		return err
	}
	if _, err := writer.Write(data); err != nil {
		return err
	}
	return nil
}

func (r *Reporter) printAsPrettyPrint(writer io.Writer) error {
	sort.Sort(r.messages)
	isFirst := true
	for _, msg := range r.messages {
		if isFirst {
			isFirst = false
		} else {
			if _, err := writer.Write([]byte("\n")); err != nil {
				return err
			}
		}
		var (
			explanation = fmt.Sprintf("%s:%d:%d: %s", msg.tok.doc.filename, msg.tok.ln, msg.tok.col, msg.content)
			lineFromDoc = msg.tok.doc.lines[msg.tok.ln-1]
			squiggle    = makeSquiggle(lineFromDoc, msg.tok)
		)
		for _, line := range []string{explanation, "\n", lineFromDoc, "\n", squiggle, "\n"} {
			if _, err := writer.Write([]byte(line)); err != nil {
				return err
			}
		}
	}
	return nil
}

// Print prints this report to the writer. For instance:
//
//     reporter.Print(os.Stderr)
func (r *Reporter) Print(writer io.Writer) error {
	if r.JSONOutput {
		return r.printAsJSON(writer)
	}
	return r.printAsPrettyPrint(writer)
}

func makeSquiggle(line string, tok token) string {
	var (
		col      int = 1
		squiggle bytes.Buffer
	)
	for _, r := range line {
		if col == tok.col {
			break
		}
		if isSeparatorSpace(r) {
			squiggle.WriteRune(r)
		} else {
			squiggle.WriteRune(' ')
		}
		col++
	}
	squiggle.WriteRune('^')
	squiggleLen := len(tok.content) - 1
	if index := strings.Index(tok.content, "\n"); index != -1 {
		squiggleLen = index - 1
	}
	squiggle.WriteString(strings.Repeat("~", squiggleLen))
	return squiggle.String()
}
