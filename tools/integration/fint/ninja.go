// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"regexp"
	"strings"
	"unicode"
)

var (
	// ruleRegex matches a regular line of Ninja stdout in the default format,
	// e.g. "[56/1234] CXX host_x64/foo.o"
	ruleRegex = regexp.MustCompile(`^\s*\[\d+/\d+\] \S+`)

	// failureStartRegex matches the first line of a failure message, e.g.
	// "FAILED: foo.o"
	failureStartRegex = regexp.MustCompile(`^\s*FAILED: .*`)

	// failureEndRegex indicates the end of Ninja's execution as a result of a
	// build failure. When present, it will be the last line of stdout.
	failureEndRegex = regexp.MustCompile(`^\s*ninja: build stopped:.*`)
)

// ninjaParser is a container for tracking the stdout of a ninja subprocess and
// aggregating the logs from any failed targets.
type ninjaParser struct {
	// ninjaStdout emits the stdout of a Ninja command.
	ninjaStdout io.Reader

	// Lines of output produced by failed Ninja steps, with all successful steps
	// filtered out to make the logs easy to read.
	failureOutputLines []string

	// Whether we're currently processing a failed step's logs (haven't yet hit
	// a line indicating the end of the error).
	processingFailure bool

	// The previous line that we checked.
	previousLine string
}

func (p *ninjaParser) parse(ctx context.Context) error {
	scanner := bufio.NewScanner(p.ninjaStdout)
	for scanner.Scan() {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		line := scanner.Text()
		p.parseLine(line)
	}
	return scanner.Err()
}

func (p *ninjaParser) parseLine(line string) {
	// Trailing whitespace isn't significant, as it doesn't affect the way the
	// line shows up in the logs. However, leading whitespace may be
	// significant, especially for compiler error messages.
	line = strings.TrimRightFunc(line, unicode.IsSpace)

	if p.processingFailure {
		if ruleRegex.MatchString(line) || failureEndRegex.MatchString(line) {
			// Found the end of the info for this failure (either a new rule
			// started or we hit the end of the Ninja logs).
			p.processingFailure = false
		} else {
			// Found another line of the error message.
			p.failureOutputLines = append(p.failureOutputLines, line)
		}
	} else if failureStartRegex.MatchString(line) {
		// We found a line that indicates the start of a build failure error
		// message. Start recording information about this failure.
		// TODO(olivernewman): Handle the case where the build doesn't even
		// start due to some irreconcilable error, e.g. multiple rules
		// generating the same target. These lines start with `ninja: error:`.
		p.processingFailure = true
		p.failureOutputLines = append(p.failureOutputLines, p.previousLine, line)
	}
	p.previousLine = line
}

func (p *ninjaParser) failureMessage() string {
	// Add a blank line at the end to ensure a trailing newline.
	lines := append(p.failureOutputLines, "")
	return strings.Join(lines, "\n")
}

func runNinja(
	ctx context.Context,
	r subprocessRunner,
	ninjaPath string,
	buildDir string,
	targets []string,
) (string, error) {
	cmd := []string{ninjaPath, "-C", buildDir}
	cmd = append(cmd, targets...)

	stdoutReader, stdoutWriter := io.Pipe()
	defer stdoutReader.Close()
	parser := &ninjaParser{ninjaStdout: stdoutReader}

	parserErrs := make(chan error)
	go func() {
		parserErrs <- parser.parse(ctx)
	}()

	var ninjaErr error
	func() {
		// Close the pipe as soon as the subprocess completes so that the pipe
		// reader will return an EOF.
		defer stdoutWriter.Close()
		ninjaErr = r.Run(ctx, cmd, io.MultiWriter(os.Stdout, stdoutWriter), os.Stderr)
	}()
	// Wait for parsing to complete.
	if parserErr := <-parserErrs; parserErr != nil {
		return "", parserErr
	}

	fmt.Println(parser.failureMessage())

	return parser.failureMessage(), ninjaErr
}
