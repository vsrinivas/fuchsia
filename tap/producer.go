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
}

// Todo returns a new Producer that prints TODO directives.
func (p *Producer) Todo() *Producer {
	newP := *p
	newP.directive = Todo
	return &newP
}

// Skip returns a new Producer that prints SKIP directives.
func (p *Producer) Skip() *Producer {
	newP := *p
	newP.directive = Skip
	return &newP
}

func (p *Producer) writeln(format string, args ...interface{}) {
	line := strings.TrimSpace(fmt.Sprintf(format, args...)) + "\n"
	fmt.Fprintf(p.writer(), line)
}

// writer initializes the Writer to use for this Producer, in case the Producer was
// initialized with nil output.
func (p *Producer) writer() io.Writer {
	if p.output == nil {
		p.output = os.Stdout
	}
	return p.output
}
