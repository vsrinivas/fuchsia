// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
	"unicode"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/streams"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

var (
	// explainRegex matches a singular line of Ninja explain stdout,
	// e.g. "ninja explain: host_x64/pm is dirty"
	explainRegex = regexp.MustCompile(`^\s*ninja explain:.*`)

	// Explicitly format Ninja stdout lines via the NINJA_STATUS
	// environment variable.  %f=finished, %t=remaining, %r=running
	ninjaStatus = "[%f/%t](%r) "

	// ruleRegex matches a line of Ninja stdout using the above ninjaStatus.
	// e.g. "[56/1234](16) CXX host_x64/foo.o"
	// $1 is the number of finished actions
	// $2 is the number of remaining actions
	// $3 is the number of running actions
	// $4 is the action type, e.g. LINK
	// $5 is the target name
	ruleRegex = regexp.MustCompile(`^\s*\[(\d+)/(\d+)\]\((\d+)\) (\S+) (\S+)`)

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

	// buildStoppedRegex indicates the end of Ninja's execution as a result of a
	// build failure. When present, it will be the last line of stdout.
	buildStoppedRegex = regexp.MustCompile(`^\s*ninja: build stopped:.*`)

	// noWorkString in the Ninja output indicates a null build (i.e. all the
	// requested targets have already been built).
	noWorkString = "\nninja: no work to do."

	// Allow dirty no-op builds, but only if they appear to be failing on these
	// paths on Mac where the filesystem has a bug that causes it to erroneously
	// report that system files do not exist when referenced via relative paths.
	// See https://fxbug.dev/61784.
	brokenMacPaths = []string{
		"/usr/bin/env",
		"/bin/ln",
		"/bin/bash",
		"/bin/sh",
		"/dev/zero",
	}

	// The following tests should never be considered affected. These tests use
	// a system image as data, so they appear affected by a broad range of
	// changes, but they're almost never actually sensitive to said changes.
	// https://fxbug.dev/67305 tracks generating this list automatically.
	neverAffectedTestLabels = []string{
		"//src/connectivity/overnet/tests/serial:overnet_serial_tests",
		"//src/recovery/simulator:recovery_simulator_boot_test",
		"//src/recovery/simulator:recovery_simulator_serial_test",
		"//src/tests/installer:installer_test",
		"//tools/fvdl/e2e:fvdl_intree_test_slirp",
		"//tools/fvdl/e2e:fvdl_intree_test_tuntap",
		"//tools/fvdl/e2e:fvdl_sdk_test",
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
	return r.runner.Run(ctx, cmd, subprocess.RunOptions{Stdout: stdout, Stderr: stderr, Env: []string{
		fmt.Sprintf("NINJA_STATUS=%s", ninjaStatus),
	}})
}

// withoutNinjaExplain is a writer that removes all Ninja explain outputs
// before writing to the underlying writer.
type withoutNinjaExplain struct {
	buf *bytes.Buffer
	w   io.Writer
}

// Write implements io.Writer for withoutNinjaExplain.
func (w *withoutNinjaExplain) Write(bs []byte) (int, error) {
	if _, err := w.buf.Write(bs); err != nil {
		return 0, err
	}
	for {
		line, err := w.buf.ReadBytes('\n')
		// Put incomplete lines back to buffer.
		if errors.Is(err, io.EOF) {
			w.buf.Write(line)
			break
		}
		if err != nil {
			return 0, err
		}
		if !explainRegex.MatchString(string(line)) {
			w.w.Write(line)
		}
	}
	return len(bs), nil
}

// Flush empties the internal buffer and forward non-ninja-explain lines.
func (w *withoutNinjaExplain) Flush() error {
	for {
		line, err := w.buf.ReadBytes('\n')
		if err != nil && !errors.Is(err, io.EOF) {
			return err
		}
		if !explainRegex.MatchString(string(line)) {
			w.w.Write(line)
		}
		// The last line may not finish with a '\n', so handle it before breaking.
		if errors.Is(err, io.EOF) {
			break
		}
	}
	return nil
}

// stripNinjaExplain returns a writer that strips all Ninja explain outputs and
// forwards the rest to input writer.
func stripNinjaExplain(w io.Writer) *withoutNinjaExplain {
	return &withoutNinjaExplain{
		buf: new(bytes.Buffer),
		w:   w,
	}
}

// ninjaParser is a container for tracking the stdio of a ninja subprocess and
// aggregating the logs from any failed targets.
type ninjaParser struct {
	// ninjaStdio emits the combined stdout and stderr of a Ninja command.
	ninjaStdio io.Reader

	// explainOutputSink is a writer to which all ninja explain output
	// will be redirected.
	explainOutputSink io.Writer

	// Lines of output produced by failed Ninja steps, with all successful steps
	// filtered out to make the logs easy to read.
	failureOutputLines []string

	// Whether we're currently processing a failed step's logs (haven't yet hit
	// a line indicating the end of the error).
	processingFailure bool

	// All lines printed for the rule currently being run, including the first
	// line that starts with an index like [0/1].
	currentRuleLines []string

	// Action statistics.
	ninjaActionData *fintpb.NinjaActionMetrics
}

func (p *ninjaParser) parse(ctx context.Context) error {
	scanner := bufio.NewScanner(p.ninjaStdio)
	for scanner.Scan() {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		line := scanner.Text()
		if err := p.parseLine(line); err != nil {
			return err
		}
	}
	return scanner.Err()
}

func (p *ninjaParser) parseLine(line string) error {
	// Trailing whitespace isn't significant, as it doesn't affect the way the
	// line shows up in the logs. However, leading whitespace may be
	// significant, especially for compiler error messages.
	line = strings.TrimRightFunc(line, unicode.IsSpace)

	ruleMatches := ruleRegex.FindStringSubmatch(line)
	if len(ruleMatches) == 6 {
		// Group each rule line with the non-rule lines of text that follow.
		p.currentRuleLines = nil

		// Ignore the line:
		//   [0/1] Regenerating ninja files
		actionIndex, err := strconv.Atoi(ruleMatches[1])
		if err == nil && actionIndex > 0 {
			// Track action counts and types.
			totalActionsTmp, err := strconv.Atoi(ruleMatches[2])
			if err != nil {
				return err
			}
			totalActions := int32(totalActionsTmp)
			if p.ninjaActionData == nil {
				p.ninjaActionData = &fintpb.NinjaActionMetrics{
					InitialActions: totalActions,
					ActionsByType:  make(map[string]int32),
				}
			}
			p.ninjaActionData.FinalActions = totalActions
			actionType := ruleMatches[4]
			p.ninjaActionData.ActionsByType[actionType]++
		}
	}
	p.currentRuleLines = append(p.currentRuleLines, line)

	if p.processingFailure {
		if ruleRegex.MatchString(line) || buildStoppedRegex.MatchString(line) {
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
		p.failureOutputLines = append(p.failureOutputLines, p.currentRuleLines...)
	} else if errorRegex.MatchString(line) {
		// An "error" log comes at the end of the output and should only be one
		// line.
		p.failureOutputLines = append(p.failureOutputLines, line)
	} else if explainRegex.MatchString(line) && p.explainOutputSink != nil {
		if _, err := p.explainOutputSink.Write([]byte(line + "\n")); err != nil {
			return err
		}
	} else if buildStoppedRegex.MatchString(line) && len(p.failureOutputLines) == 0 {
		// Use the "build stopped" error message (which is generally the final
		// line of output) as the failure message if there were no failed
		// targets, as this is likely an internal ninja failure that the "build
		// stopped" message will help identify.
		p.failureOutputLines = append(p.failureOutputLines, line)
	}
	return nil
}

func (p *ninjaParser) failureMessage() string {
	if len(p.failureOutputLines) == 0 {
		p.failureOutputLines = []string{unrecognizedFailureMsg}
	}
	lines := p.failureOutputLines
	if p.failureOutputLines[len(p.failureOutputLines)-1] != "" {
		// Add a blank line at the end to ensure a trailing newline.
		lines = append(p.failureOutputLines, "")
	}
	return strings.Join(lines, "\n")
}

// runNinja runs ninja as a subprocess to build the specified targets.
func runNinja(
	ctx context.Context,
	r ninjaRunner,
	ninjaArgs []string,
	targets []string,
	explain bool,
	explainSink io.Writer,
) (string, *fintpb.NinjaActionMetrics, error) {
	stdioReader, stdioWriter := io.Pipe()
	defer stdioReader.Close()
	parser := &ninjaParser{ninjaStdio: stdioReader, explainOutputSink: explainSink}

	parserErrs := make(chan error)
	go func() {
		parserErrs <- parser.parse(ctx)
	}()

	err := func() error {
		// Close the pipe as soon as the subprocess completes so that the pipe
		// reader will return an EOF.
		defer stdioWriter.Close()
		if explain {
			targets = append(targets, "-d", "explain")
		}
		stdout, stderr := stripNinjaExplain(streams.Stdout(ctx)), stripNinjaExplain(streams.Stderr(ctx))
		err := r.run(
			ctx,
			append(ninjaArgs, targets...),
			// Ninja writes "ninja: ..." logs to stderr, but step logs like
			// "[1/12345] ..." to stdout. The parser should consider both of
			// these kinds of logs. In theory stdout and stderr should be
			// handled by separate parsers, but it's simpler to dump them to the
			// same stream and have the parser read from that stream. The writer
			// returned by io.Pipe() is thread-safe, so there's no need to worry
			// about interleaving characters of stdout and stderr.
			//
			// Ninja explain outputs are not stripped from stdout and stderr because
			// there are too much.
			io.MultiWriter(stdout, stdioWriter),
			io.MultiWriter(stderr, stdioWriter),
		)
		if err != nil {
			return err
		}
		if err := stdout.Flush(); err != nil {
			return fmt.Errorf("flushing stdout writer: %w", err)
		}
		if err := stderr.Flush(); err != nil {
			return fmt.Errorf("flushing stderr writer: %w", err)
		}
		return nil
	}()
	// Wait for parsing to complete.
	if parserErr := <-parserErrs; parserErr != nil {
		return "", nil, parserErr
	}

	if err != nil {
		return parser.failureMessage(), parser.ninjaActionData, err
	}

	// No failure message necessary if Ninja succeeded.
	return "", parser.ninjaActionData, nil
}

// ninjaDryRun does a `ninja explain` dry run against a build directory and
// returns the stdout and stderr.
func ninjaDryRun(ctx context.Context, r ninjaRunner, targets []string) (string, string, error) {
	// -n means dry-run.
	args := []string{"-d", "explain", "--verbose", "-n"}
	args = append(args, targets...)

	var stdout, stderr bytes.Buffer
	err := r.run(ctx, args, &stdout, &stderr)
	if err != nil {
		// stdout and stderr are normally not emitted because they're very
		// noisy, but if the dry run fails then they'll likely contain the
		// information necessary to understand the failure.
		streams.Stdout(ctx).Write(stdout.Bytes())
		streams.Stderr(ctx).Write(stderr.Bytes())
	}
	return stdout.String(), stderr.String(), err
}

// checkNinjaNoop runs `ninja explain` against a build directory to determine
// whether an incremental build would be a no-op (i.e. all requested targets
// have already been built). It returns true if the build would be a no-op,
// false otherwise.
//
// It also returns the first line of ninja's output, which often contains a
// useful message, and a map of logs produced by the no-op check, which can be
// presented to the user for help with debugging in case the check fails.
func checkNinjaNoop(
	ctx context.Context,
	r ninjaRunner,
	targets []string,
	isMac bool,
) (bool, string, map[string]string, error) {
	stdout, stderr, ninjaErr := ninjaDryRun(ctx, r, targets)
	// Temporarily tolerate a failure if it's on Mac. We won't emit the error if
	// it seemed to be caused by a known broken Mac path.
	if ninjaErr != nil && !isMac {
		return false, "", nil, ninjaErr
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
					return true, "", nil, nil
				}
			}
		}
		logs := map[string]string{
			"`ninja -d explain -v -n` stdout": stdout,
			"`ninja -d explain -v -n` stderr": stderr,
		}
		// Return the original ninja error, which may be non-nil if we're
		// running on a Mac and the dry run failed but the stdio didn't contain
		// one of the broken Mac paths.
		noopMsg := strings.Split(stderr, "\n")[0]
		noopMsg = strings.TrimPrefix(noopMsg, "ninja explain: ")
		return false, noopMsg, logs, ninjaErr
	}

	return true, "", nil, nil
}

// touchFiles updates the modified time on all the specified files to the
// current timestamp, skipping any nonexistent files.
// Returns a map of paths touched to their previous stats.
// This map can be passed to resetTouchedFiles to revert the operation.
func touchFiles(paths []string) (map[string]time.Time, error) {
	reset := make(map[string]time.Time)
	now := time.Now()
	for _, path := range paths {
		stat, err := os.Stat(path)
		if err != nil {
			// Skip any paths that don't exist, e.g. because the file was deleted in
			// the change under test.
			if os.IsNotExist(err) {
				continue
			}
			return nil, err
		}
		// Note that we can't get access time in a platform-agnostic way.
		// We end up coupling mtime with atime, even after a reset.
		reset[path] = stat.ModTime()
		if err := os.Chtimes(path, now, now); err != nil {
			return nil, err
		}
	}
	return reset, nil
}

// Rolls back changes made by a previous call to touchFiles.
func resetTouchFiles(touchFilesResult map[string]time.Time) error {
	for path, mtime := range touchFilesResult {
		if err := os.Chtimes(path, mtime, mtime); err != nil {
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
	contextSpec *fintpb.Context,
	allTests []build.Test,
	targets []string,
) (affectedTestsResult, error) {
	result := affectedTestsResult{
		logs: map[string]string{},
	}

	// Map from "... is dirty" line printed by Ninja to affected test
	testsByDirtyLine := map[string][]string{}
	// Map from test path (if defined) to test name
	testsByPath := map[string]string{}
	// Map from path to BUILD.gn file defining the test to the test
	testsByBuildGn := map[string][]string{}

	for _, test := range allTests {
		// Ignore any tests that shouldn't be considered affected.
		labelNoToolchain := strings.Split(test.Label, "(")[0]
		if contains(neverAffectedTestLabels, labelNoToolchain) {
			continue
		}

		// For host tests we use the executable path.
		if test.Path != "" {
			testsByPath[test.Path] = test.Name
		}

		for _, packageManifest := range test.PackageManifests {
			dirtyLine := dirtyLineForPackageManifest(packageManifest)
			testsByDirtyLine[dirtyLine] = append(testsByDirtyLine[dirtyLine], test.Name)
		}

		buildGnPath := buildGnPathForLabel(test.Label)
		testsByBuildGn[buildGnPath] = append(testsByBuildGn[buildGnPath], test.Name)
		if test.PackageLabel != "" {
			buildGnPath = buildGnPathForLabel(test.PackageLabel)
			testsByBuildGn[buildGnPath] = append(testsByBuildGn[buildGnPath], test.Name)
		}
	}

	var gnFiles, nonGNFiles []string
	for _, f := range contextSpec.ChangedFiles {
		ext := filepath.Ext(f.Path)
		if ext == ".gn" || ext == ".gni" {
			gnFiles = append(gnFiles, f.Path)
		} else {
			nonGNFiles = append(nonGNFiles, f.Path)
		}
	}

	var affectedTests []string
	for _, gnFile := range gnFiles {
		gnFile = strings.TrimPrefix(gnFile, "build/secondary/")
		match, ok := testsByBuildGn[gnFile]
		if ok {
			affectedTests = append(affectedTests, match...)
		}
	}

	// Our Ninja graph is set up in such a way that touching any GN files
	// triggers an action to regenerate the entire graph. So if GN files were
	// modified and we touched them then the following dry run results are not
	// useful for determining affected tests.
	touchNonGNResult, err := touchFiles(makeAbsolute(contextSpec.CheckoutDir, nonGNFiles))
	if err != nil {
		return result, err
	}
	defer resetTouchFiles(touchNonGNResult)
	stdout, stderr, err := ninjaDryRun(ctx, runner, targets)
	if err != nil {
		return result, err
	}
	ninjaOutput := strings.Join([]string{stdout, stderr}, "\n\n")

	for _, line := range strings.Split(ninjaOutput, "\n") {
		match, ok := testsByDirtyLine[line]
		if ok {
			// Matched an expected line
			affectedTests = append(affectedTests, match...)
		} else {
			// Look for actions that reference host test path. Different types
			// of host tests have different actions, but they all mention the
			// final executable path.
			// fxbug.dev(85524): tokenize with shlex in case test paths include
			// whitespace.
			for _, maybeTestPath := range strings.Split(line, " ") {
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
		touchGNResult, err := touchFiles(makeAbsolute(contextSpec.CheckoutDir, gnFiles))
		if err != nil {
			return result, err
		}
		defer resetTouchFiles(touchGNResult)
		var stdout, stderr string
		stdout, stderr, err = ninjaDryRun(ctx, runner, targets)
		if err != nil {
			return result, err
		}
		ninjaOutput = strings.Join([]string{stdout, stderr}, "\n\n")
	}
	result.logs["ninja dry run output"] = ninjaOutput
	result.noWork = strings.Contains(ninjaOutput, noWorkString)
	result.affectedTests = removeDuplicates(affectedTests)

	return result, nil
}

func dirtyLineForPackageManifest(label string) string {
	return "ninja explain: " + label + " is dirty"
}

func buildGnPathForLabel(label string) string {
	result := strings.TrimPrefix(label, "//")
	result = strings.Split(result, ":")[0]
	return path.Join(result, "BUILD.gn")
}

// ninjaGraph runs the ninja graph tool and pipes its stdout to the file at the
// given path.
func ninjaGraph(ctx context.Context, r ninjaRunner, targets []string, graphPath string) error {
	f, err := osmisc.CreateFile(graphPath)
	if err != nil {
		return err
	}
	defer f.Close()
	args := append([]string{"-t", "graph"}, targets...)
	return r.run(ctx, args, f, os.Stderr)
}

// ninjaCompdb runs the ninja compdb tool and pipes its stdout to the file at
// the given path.
func ninjaCompdb(ctx context.Context, r ninjaRunner, compdbPath string) error {
	f, err := osmisc.CreateFile(compdbPath)
	if err != nil {
		return err
	}
	defer f.Close()
	// Don't specify targets, as we want all build edges to be generated.
	args := []string{"-t", "compdb"}
	return r.run(ctx, args, f, os.Stderr)
}
