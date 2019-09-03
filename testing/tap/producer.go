// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tap

import (
	"fmt"
	"io"
	"os"
	"strings"
)

// Producer produces TAP output.
//
// This producer always includes test numbers.
type Producer struct {
	output     io.Writer
	directive  Directive
	testNumber int
}

// NewProducer creates a new Producer that writes to the given Writer.
func NewProducer(w io.Writer) *Producer {
	p := &Producer{
		output:    w,
		directive: None,
	}
	// Output the TAP version line.
	p.writeln("TAP version 13")
	return p
}

// Plan outputs the TAP plan line: 1..count.  If count <= 0, nothing is printed
func (p *Producer) Plan(count int) {
	if count > 0 {
		p.writeln("1..%d", count)
	}
}

// Ok outputs a test line containing the given description and starting with either "ok"
// if test is true or "not ok" if false.  If this producer was created using
// Todo or Skip, then the corresponding directive is also printed, and the description is
// used as the explanation of that directive.
func (p *Producer) Ok(test bool, description string) {
	p.testNumber++

	ok := "ok"
	if !test {
		ok = "not ok"
	}

	switch p.directive {
	case None:
		p.writeln("%s %d %s", ok, p.testNumber, description)
	case Todo:
		p.writeln("%s %d # TODO %s", ok, p.testNumber, description)
	case Skip:
		p.writeln("%s %d # SKIP %s", ok, p.testNumber, description)
	}

	p.directive = None
}

// YAML produces a YAML block from the given input. This will indent the input data. The
// caller should not do this themselves.
func (p *Producer) YAML(input []byte) {
	// Chomp empty lines from the end of the document.
	content := strings.TrimSuffix(string(input), "\n")
	p.writeln(p.indent("---"))
	p.writeln(p.indent(content))
	p.writeln(p.indent("..."))
}

// Todo returns a new Producer that prints TODO directives.
func (p *Producer) Todo() *Producer {
	p.directive = Todo
	return p
}

// Skip returns a new Producer that prints SKIP directives.
func (p *Producer) Skip() *Producer {
	p.directive = Skip
	return p
}

func (p *Producer) writeln(format string, args ...interface{}) {
	fmt.Fprintln(p.writer(), fmt.Sprintf(format, args...))
}

// writer initializes the Writer to use for this Producer, in case the Producer was
// initialized with nil output.
func (p *Producer) writer() io.Writer {
	if p.output == nil {
		p.output = os.Stdout
	}
	return p.output
}

// Indent indents every line of the input text with a single space.
func (p *Producer) indent(input string) string {
	return string(p.indentBytes([]byte(input)))
}

// IndentBytes indents every line of the input text with a single space.
func (p *Producer) indentBytes(input []byte) []byte {
	var output []byte
	startOfLine := true
	for _, c := range input {
		if startOfLine && c != '\n' {
			output = append(output, ' ')
		}
		output = append(output, c)
		startOfLine = c == '\n'
	}
	return output
}
