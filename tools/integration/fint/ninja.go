// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"
	"time"
	"unicode"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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

	// The following tests should never be considered affected. These tests use
	// a system image as data, so they appear affected by a broad range of
	// changes, but they're almost never actually sensitive to said changes.
	// https://fxbug.dev/67305 tracks generating this list automatically.
	neverAffectedTestLabels = []string{
		"//src/connectivity/overnet/tests/serial:overnet_serial_tests",
		"//src/recovery/simulator:recovery_simulator_boot_test",
		"//src/recovery/simulator:recovery_simulator_serial_test",
		"//tools/fvdl/e2e:fvdl_intree_test",
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

// ninjaDryRun does a `ninja explain` dry run against a build directory and
// returns the stdout and stderr.
func ninjaDryRun(ctx context.Context, r ninjaRunner, targets []string) (string, string, error) {
	// -n means dry-run.
	args := []string{"-d", "explain", "--verbose", "-n"}
	args = append(args, targets...)

	var stdout, stderr strings.Builder
	err := r.run(ctx, args, &stdout, &stderr)
	return stdout.String(), stderr.String(), err
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
) (bool, map[string]string, error) {
	stdout, stderr, err := ninjaDryRun(ctx, r, targets)
	if err != nil {
		return false, nil, err
	}

	// Different versions of Ninja choose to emit "explain" logs to stderr
	// instead of stdout, so we want to analyze both streams.
	// Concatenate the two streams for simplicity so that we don't need to do
	// the same operation separately on each stream.
	allStdio := strings.Join([]string{stdout, stderr}, "\n\n")
	if !strings.Contains(allStdio, noWorkString) {
		if isMac {
			// TODO(https://fxbug.dev/61784): Dirty builds should be an error even on Mac.
			for _, path := range brokenMacPaths {
				if strings.Contains(allStdio, path) {
					return true, nil, nil
				}
			}
		}
		logs := map[string]string{
			"`ninja -d explain -v -n` stdout": stdout,
			"`ninja -d explain -v -n` stderr": stderr,
		}
		return false, logs, nil
	}

	return true, nil, nil
}

// touchFiles updates the modified time on all the specified files to the
// current timestamp, skipping any nonexistent files.
func touchFiles(ctx context.Context, paths []string) error {
	now := time.Now()
	for _, path := range paths {
		// TODO(fxbug.dev/75371): Delete this log once we're done debugging
		// discrepancies between fint and affected_tests.py.
		logger.Debugf(ctx, "Touch: %s", path)
		err := os.Chtimes(path, now, now)
		// Skip any paths that don't exist, e.g. because the file was deleted in
		// the change under test.
		if err != nil && !os.IsNotExist(err) {
			return err
		}
	}
	return nil
}

// affectedTestsResult is the type emitted by `affectedTestsNoWork()`. It exists
// solely to keep return statements in that function concise.
type affectedTestsResult struct {
	// Names of tests that are affected based on the paths of the changed files.
	affectedTests []string

	// Whether the build graph is unaffected by the changed files.
	noWork bool

	// Keep track of logs so the caller can choose to present them to the user
	// for debugging purposes.
	logs map[string]string
}

// affectedTestsNoWork touches affected files and then does a ninja dry run and
// analyzes the output, to determine:
// a) If the build graph is affected by the changed files.
// b) If so, which tests are affected by the changed files.
func affectedTestsNoWork(
	ctx context.Context,
	runner ninjaRunner,
	allTests []build.Test,
	affectedFiles []string,
	targets []string,
) (affectedTestsResult, error) {
	result := affectedTestsResult{
		logs: map[string]string{},
	}

	testsByStamp := map[string][]string{}
	testsByPath := map[string]string{}

	for _, test := range allTests {
		// Ignore any tests that shouldn't be considered affected.
		labelNoToolchain := strings.Split(test.Label, "(")[0]
		if contains(neverAffectedTestLabels, labelNoToolchain) {
			continue
		}

		path := test.Path
		// For host tests we use the executable path.
		if path != "" {
			testsByPath[path] = test.Name
			continue
		}
		// For Fuchsia tests we derive the stamp path from the GN label.
		stamp, err := stampFileForTest(test.Label)
		if err != nil {
			return result, err
		}
		testsByStamp[stamp] = append(testsByStamp[stamp], test.Name)
	}

	// Keep track of the files' original modification times so we can reset them
	// before exiting. Otherwise they will affect the results of the
	// affected_tests.py script.
	// TODO(fxbug.dev/75371): Stop resetting the mtimes once we stop running
	// affected_tests.py after fint.
	originalModTimes := map[string]time.Time{}
	for _, path := range affectedFiles {
		fi, err := os.Stat(path)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return result, err
		}
		originalModTimes[path] = fi.ModTime()
	}

	var gnFiles, nonGNFiles []string
	for _, path := range affectedFiles {
		ext := filepath.Ext(path)
		if ext == ".gn" || ext == ".gni" {
			gnFiles = append(gnFiles, path)
		} else {
			nonGNFiles = append(nonGNFiles, path)
		}
	}

	// Our Ninja graph is set up in such a way that touching any GN files
	// triggers an action to regenerate the entire graph. So if GN files were
	// modified and we touched them then the following dry run results are not
	// useful for determining affected tests.
	if err := touchFiles(ctx, nonGNFiles); err != nil {
		return result, err
	}
	stdout, stderr, err := ninjaDryRun(ctx, runner, targets)
	if err != nil {
		return result, err
	}
	ninjaOutput := strings.Join([]string{stdout, stderr}, "\n\n")

	var affectedTests []string
	for _, line := range strings.Split(ninjaOutput, "\n") {
		// Trim the bracketed progress number from the beginning of the line.
		split := strings.Split(line, "] ")
		if len(split) < 2 {
			continue
		}
		action := split[1]
		if strings.HasPrefix(action, "touch ") && strings.Contains(action, "obj/") {
			// Matches actions like "touch baz/obj/foo/bar.stamp".
			stampFile := action[strings.Index(action, "obj/"):]
			affectedTests = append(affectedTests, testsByStamp[stampFile]...)
		} else {
			// Look for actions that reference host test path. Different types
			// of host tests have different actions, but they all mention the
			// final executable path.
			for _, maybeTestPath := range strings.Split(action, " ") {
				maybeTestPath = strings.Trim(maybeTestPath, `"`)
				testName, ok := testsByPath[maybeTestPath]
				if !ok {
					continue
				}
				affectedTests = append(affectedTests, testName)
			}
		}
	}

	// For determination of "no work to do", we want to consider all files,
	// *including* GN files. If no GN files are affected, then we already have
	// the necessary output from the first ninja dry run, so we can skip doing
	// the second dry run that includes GN files.
	if len(gnFiles) > 0 {
		result.logs["ninja dry run output (no GN files)"] = ninjaOutput

		// Since we only did a Ninja dry run, the non-GN files will still be
		// considered dirty, so we need only touch the GN files.
		if err = touchFiles(ctx, gnFiles); err != nil {
			return result, err
		}
		var stdout, stderr string
		stdout, stderr, err = ninjaDryRun(ctx, runner, targets)
		if err != nil {
			return result, err
		}
		ninjaOutput = strings.Join([]string{stdout, stderr}, "\n\n")
	}
	result.logs["ninja dry run output"] = ninjaOutput
	result.noWork = strings.Contains(ninjaOutput, noWorkString)

	// TODO(fxbug.dev/75371): Stop resetting the mtimes once we stop running
	// affected_tests.py after fint.
	for _, path := range affectedFiles {
		mtime, ok := originalModTimes[path]
		if !ok {
			// If the file doesn't exist, it won't be present in `originalModTimes`.
			continue
		}
		if err := os.Chtimes(path, mtime, mtime); err != nil {
			return result, err
		}
	}

	result.affectedTests = removeDuplicates(affectedTests)

	return result, nil
}

func stampFileForTest(label string) (string, error) {
	// Remove the "//" prefix and parenthesized toolchain suffix from the label.
	trimmedLabel := strings.TrimPrefix(strings.Split(label, "(")[0], "//")

	var directory, targetName string
	if strings.Contains(trimmedLabel, ":") {
		// Label looks like "//foo/bar:baz"
		parts := strings.Split(trimmedLabel, ":")
		directory = parts[0]
		targetName = parts[1]
	} else {
		// Label looks like "//foo/bar"
		directory = trimmedLabel
		targetName = path.Base(trimmedLabel)
	}

	if directory == "" || targetName == "" {
		return "", fmt.Errorf("failed to parse test label %q", label)
	}

	var stampPath string
	if targetName == path.Base(directory) {
		stampPath = directory
	} else {
		stampPath = path.Join(directory, targetName)
	}

	// GN labels always use forward slashes, so make sure to convert to
	// platform-specific file path format when returning the stamp file path.
	return filepath.Join("obj", filepath.FromSlash(stampPath)+".stamp"), nil
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
