// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"regexp"
	"strings"
	"unicode"
)

var (
	// ruleRegex matches a regular line of Ninja stdout in the default format,
	// e.g. "[56/1234] CXX host_x64/foo.o"
	ruleRegex = regexp.MustCompile(`^\s*\[\d+/\d+\] \S+`)

	// errorRegex matches a single-line error message that Ninja prints at the
	// end of its output if it encounters an error that prevents it from even
	// starting the build (e.g. multiple rules generating the same target file),
	// or a fatal error in the middle of the build (e.g. OS error). These error
	// strings are defined here:
	// https://github.com/ninja-build/ninja/blob/master/src/util.cc
	errorRegex = regexp.MustCompile(`^\s*ninja: (error|fatal): .+`)

	// failureStartRegex matches the first line of a failure message, e.g.
	// "FAILED: foo.o"
	failureStartRegex = regexp.MustCompile(`^\s*FAILED: .*`)

	// failureEndRegex indicates the end of Ninja's execution as a result of a
	// build failure. When present, it will be the last line of stdout.
	failureEndRegex = regexp.MustCompile(`^\s*ninja: build stopped:.*`)

	// noWorkString in the Ninja output indicates a null build (i.e. all the
	// requested targets have already been built).
	noWorkString = "\nninja: no work to do."

	// Allow dirty no-op builds, but only if they appear to be failing on these
	// paths on Mac where the filesystem has a bug. See https://fxbug.dev/61784.
	brokenMacPaths = []string{
		"/usr/bin/env",
		"/bin/ln",
		"/bin/bash",
	}
)

const (
	// unrecognizedFailureMsg is the message we'll output if ninja fails but its
	// output doesn't match any of the known failure modes.
	unrecognizedFailureMsg = "Unrecognized failures, please check the original stdout instead."
)

// ninjaRunner provides logic for running ninja commands using common flags
// (e.g. build directory name).
type ninjaRunner struct {
	runner    subprocessRunner
	ninjaPath string
	buildDir  string
	jobCount  int
}

// run runs a ninja command as a subprocess, passing `args` in addition to the
// common args configured on the ninjaRunner.
func (r ninjaRunner) run(ctx context.Context, args []string, stdout, stderr io.Writer) error {
	cmd := []string{r.ninjaPath, "-C", r.buildDir}
	if r.jobCount > 0 {
		cmd = append(cmd, "-j", fmt.Sprintf("%d", r.jobCount))
	}
	cmd = append(cmd, args...)
	return r.runner.Run(ctx, cmd, stdout, stderr)
}

// ninjaParser is a container for tracking the stdio of a ninja subprocess and
// aggregating the logs from any failed targets.
type ninjaParser struct {
	// ninjaStdio emits the combined stdout and stderr of a Ninja command.
	ninjaStdio io.Reader

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
	scanner := bufio.NewScanner(p.ninjaStdio)
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
		p.processingFailure = true
		p.failureOutputLines = append(p.failureOutputLines, p.previousLine, line)
	} else if errorRegex.MatchString(line) {
		// An "error" log comes at the end of the output and should only be one
		// line.
		p.failureOutputLines = append(p.failureOutputLines, line)
	}
	p.previousLine = line
}

func (p *ninjaParser) failureMessage() string {
	if len(p.failureOutputLines) == 0 {
		return unrecognizedFailureMsg
	}
	// Add a blank line at the end to ensure a trailing newline.
	lines := append(p.failureOutputLines, "")
	return strings.Join(lines, "\n")
}

func runNinja(
	ctx context.Context,
	r ninjaRunner,
	targets []string,
	explain bool,
) (string, error) {
	stdioReader, stdioWriter := io.Pipe()
	defer stdioReader.Close()
	parser := &ninjaParser{ninjaStdio: stdioReader}

	parserErrs := make(chan error)
	go func() {
		parserErrs <- parser.parse(ctx)
	}()

	var ninjaErr error
	func() {
		// Close the pipe as soon as the subprocess completes so that the pipe
		// reader will return an EOF.
		defer stdioWriter.Close()
		if explain {
			targets = append(targets, "-d", "explain")
		}
		ninjaErr = r.run(
			ctx,
			targets,
			// Ninja writes "ninja: ..." logs to stderr, but step logs like
			// "[1/12345] ..." to stdout. The parser should consider both of
			// these kinds of logs. In theory stdout and stderr should be
			// handled by separate parsers, but it's simpler to dump them to the
			// same stream and have the parser read from that stream. The writer
			// returned by io.Pipe() is thread-safe, so there's no need to worry
			// about interleaving characters of stdout and stderr.
			io.MultiWriter(os.Stdout, stdioWriter),
			io.MultiWriter(os.Stderr, stdioWriter),
		)
	}()
	// Wait for parsing to complete.
	if parserErr := <-parserErrs; parserErr != nil {
		return "", parserErr
	}

	if ninjaErr != nil {
		return parser.failureMessage(), ninjaErr
	}

	// No failure message necessary if Ninja succeeded.
	return "", nil
}

// checkNinjaNoop runs `ninja explain` against a build directory to determine
// whether an incremental build would be a no-op (i.e. all requested targets
// have already been built). It returns true if the build would be a no-op,
// false otherwise.
//
// It also returns a map of logs produced by the no-op check, which can be
// presented to the user for help with debugging in case the check fails.
func checkNinjaNoop(
	ctx context.Context,
	r ninjaRunner,
	targets []string,
	isMac bool,
) (bool, map[string]*bytes.Buffer, error) {
	// -n means dry-run.
	args := []string{"-d", "explain", "--verbose", "-n"}
	args = append(args, targets...)

	var stdout, stderr bytes.Buffer
	if err := r.run(ctx, args, &stdout, &stderr); err != nil {
		return false, nil, err
	}

	outputContains := func(s string) bool {
		b := []byte(s)
		// Different versions of Ninja choose to emit "explain" logs to stderr
		// instead of stdout, so check both streams.
		return bytes.Contains(stdout.Bytes(), b) || bytes.Contains(stderr.Bytes(), b)
	}

	if !outputContains(noWorkString) {
		if isMac {
			// TODO(https://fxbug.dev/61784): Dirty builds should be an error even on Mac.
			for _, path := range brokenMacPaths {
				if outputContains(path) {
					return true, nil, nil
				}
			}
		}
		logs := map[string]*bytes.Buffer{
			"`ninja -d explain -v -n` stdout": &stdout,
			"`ninja -d explain -v -n` stderr": &stderr,
		}
		return false, logs, nil
	}

	return true, nil, nil
}

// ninjaGraph runs the ninja graph tool and pipes its stdout to a temporary
// file, returning the path to the resulting file.
func ninjaGraph(ctx context.Context, r ninjaRunner, targets []string) (string, error) {
	graphFile, err := ioutil.TempFile("", "*-graph.dot")
	if err != nil {
		return "", err
	}
	defer graphFile.Close()
	args := append([]string{"-t", "graph"}, targets...)
	if err := r.run(ctx, args, graphFile, os.Stderr); err != nil {
		return "", err
	}
	return graphFile.Name(), nil
}

// ninjaCompdb runs the ninja compdb tool and pipes its stdout to a temporary
// file, returning the path to the resulting file.
func ninjaCompdb(ctx context.Context, r ninjaRunner) (string, error) {
	compdbFile, err := ioutil.TempFile("", "*-compile-commands.json")
	if err != nil {
		return "", err
	}
	defer compdbFile.Close()
	// Don't specify targets, as we want all build edges to be generated.
	args := []string{"-t", "compdb"}
	if err := r.run(ctx, args, compdbFile, os.Stderr); err != nil {
		return "", err
	}
	return compdbFile.Name(), nil
}

func ninjaCleanDead(ctx context.Context, r ninjaRunner) error {
	return r.run(ctx, []string{"-t", "cleandead"}, os.Stdout, os.Stderr)
}
