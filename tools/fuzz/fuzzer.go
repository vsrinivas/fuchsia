// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/golang/glog"
)

// A Fuzzer represents a fuzzer present on an instance.
type Fuzzer struct {
	build Build
	// Name is `package/binary`
	Name string

	pkg      string
	manifest string
	pkgUrl   string
	url      string
	args     []string
	options  map[string]string
}

// 1 is the default `exitcode` from SanitizerCommonFlags
// 7x are from compiler-rt/lib/fuzzer/FuzzerOptions.h
var expectedFuzzerReturnCodes = []int{
	0,  // no crash
	1,  // sanitizer error
	70, // libFuzzer timeout
	71, // libFuzzer OOM
	77, // libFuzzer crash
}

// NewV1Fuzzer and NewV2Fuzzer construct a fuzzer object with the given pkg/fuzzer name
func NewV1Fuzzer(build Build, pkg, fuzzer string) *Fuzzer {
	return newFuzzer(build, pkg, fuzzer, "cmx")
}
func NewV2Fuzzer(build Build, pkg, fuzzer string) *Fuzzer {
	return newFuzzer(build, pkg, fuzzer, "cm")
}
func newFuzzer(build Build, pkg, fuzzer, manifestSuffix string) *Fuzzer {
	return &Fuzzer{
		build:    build,
		Name:     fmt.Sprintf("%s/%s", pkg, fuzzer),
		pkg:      pkg,
		manifest: fmt.Sprintf("%s.%s", fuzzer, manifestSuffix),
		pkgUrl:   "fuchsia-pkg://fuchsia.com/" + pkg,
		url: fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s#meta/%s.%s",
			pkg, fuzzer, manifestSuffix),
	}
}

func (f *Fuzzer) IsExample() bool {
	// Temporarily allow specific examples through for testing ClusterFuzz behavior in production
	return f.pkg == "example-fuzzers" &&
		!(f.Name == "example-fuzzers/out_of_memory_fuzzer" ||
			f.Name == "example-fuzzers/toy_example_arbitrary")
}

// Return whether or not this is a Component Fuzzing Framework fuzzer
func (f *Fuzzer) isV2() bool {
	return strings.HasSuffix(f.manifest, ".cm")
}

// Map paths as referenced by ClusterFuzz to internally-used paths as seen by
// libFuzzer, SFTP, etc.
func (f *Fuzzer) translatePath(relpath string) string {
	if f.isV2() {
		// Not necessary for v2
		return relpath
	}

	// Note: we can't use path.Join or other path functions that normalize the path
	// because it will drop trailing slashes, which is important to preserve in
	// places like artifact_prefix.

	// Rewrite all references to data/ to tmp/ for better performance
	if strings.HasPrefix(relpath, "data/") {
		relpath = "tmp/" + strings.TrimPrefix(relpath, "data/")
	}

	return relpath
}

// AbsPath returns the absolute target path for a given relative path in a
// fuzzer package. The path may differ depending on whether it is identified as
// a resource, data, or neither.
func (f *Fuzzer) AbsPath(relpath string) string {
	if f.isV2() {
		if strings.HasPrefix(relpath, cachePrefix) {
			// No-op if already "absolute"
			return relpath
		} else {
			// Extremely basic path normalization
			if !strings.HasPrefix(relpath, "/") {
				relpath = "/" + relpath
			}
			urlAsPath := strings.ReplaceAll(f.url, "/", "_")
			urlAsPath = strings.ReplaceAll(urlAsPath, "#", "__")
			return path.Join(cachePrefix+urlAsPath, relpath)
		}
	}

	relpath = f.translatePath(relpath)

	if strings.HasPrefix(relpath, "/") {
		return relpath
	} else if strings.HasPrefix(relpath, "pkg/") {
		return fmt.Sprintf("/pkgfs/packages/%s/0/%s", f.pkg, relpath[4:])
	} else if strings.HasPrefix(relpath, "data/") {
		return fmt.Sprintf("/data/r/sys/fuchsia.com:%s:0#meta:%s/%s",
			f.pkg, f.manifest, relpath[5:])
	} else if strings.HasPrefix(relpath, "tmp/") {
		return fmt.Sprintf("/tmp/r/sys/fuchsia.com:%s:0#meta:%s/%s",
			f.pkg, f.manifest, relpath[4:])
	} else {
		return fmt.Sprintf("/%s", relpath)
	}
}

// Parse command line arguments for the fuzzer. For '-key=val' style options,
// the last 'val' for a given 'key' is used. Any previously parsed options will
// be discarded.
func (f *Fuzzer) Parse(args []string) {
	f.args = []string{}
	f.options = make(map[string]string)
	// For reference, see libFuzzer's flag-parsing method (`FlagValue`) in:
	// https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/fuzzer/FuzzerDriver.cpp
	re := regexp.MustCompile(`^-([^-=\s]+)=(.*)$`)
	for _, arg := range args {
		submatch := re.FindStringSubmatch(arg)
		if submatch == nil {
			f.args = append(f.args, f.translatePath(arg))
		} else {
			f.options[submatch[1]] = f.translatePath(submatch[2])
		}
	}
}

// Fetch and echo the syslog for the given `pid` to `out`
func dumpSyslog(pid int, conn Connector, out io.Writer) error {
	if pid == 0 {
		return fmt.Errorf("failed to fetch syslog: missing pid")
	}
	log, err := conn.GetSysLog(pid)
	if err != nil {
		return fmt.Errorf("failed to fetch syslog: %s", err)
	}
	io.WriteString(out, log+"\n")
	return nil
}

func scanForPIDs(conn Connector, out io.WriteCloser, in io.ReadCloser) chan error {
	scanErr := make(chan error, 1)

	go func() {
		// Propagate EOFs, so that:
		// - The symbolizer terminates properly.
		defer out.Close()
		// - The fuzzer doesn't block if an early exit occurs later in the chain.
		defer in.Close()

		// mutRegex detects output from
		// MutationDispatcher::PrintMutationSequence
		// (compiler-rt/lib/fuzzer/FuzzerMutate.cpp), which itself is called
		// from Fuzzer::DumpCurrentUnit (compiler-rt/lib/fuzzer/FuzzerLoop.cpp)
		// as part of exit/crash callbacks
		mutRegex := regexp.MustCompile(`^MS: [0-9]*`)
		pidRegex := regexp.MustCompile(`^==([0-9]+)==`)
		sawMut := false
		sawPid := false
		scanner := bufio.NewScanner(in)
		pid := 0
		for scanner.Scan() {
			line := scanner.Text()
			if _, err := io.WriteString(out, line+"\n"); err != nil {
				scanErr <- fmt.Errorf("error writing: %s", err)
				return
			}

			if m := pidRegex.FindStringSubmatch(line); m != nil {
				pid, _ = strconv.Atoi(m[1]) // guaranteed parseable due to regex
				glog.Infof("Found fuzzer PID: %d", pid)
				if sawPid {
					glog.Warningf("Saw multiple PIDs; ignoring")
					continue
				}
				sawPid = true
			}

			if mutRegex.MatchString(line) {
				glog.Infof("Found mutation sequence: %s", line)
				if sawMut {
					glog.Warningf("Saw multiple mutation sequences; ignoring")
					continue
				}
				sawMut = true
				if err := dumpSyslog(pid, conn, out); err != nil {
					glog.Warning(err)
					// Include this warning inline so it is visible in fuzzer logs
					fmt.Fprintf(out, "WARNING: %s\n", err)
				}
			}
		}

		if err := scanner.Err(); err != nil {
			scanErr <- err
		}

		// If we haven't already dumped the syslog inline, do it here now that
		// the process has exited.  This will happen in non-fuzzing cases, such
		// as repro runs.
		if !sawMut {
			if err := dumpSyslog(pid, conn, out); err != nil {
				glog.Warning(err)
				// Include this warning inline so it is visible in fuzzer logs
				fmt.Fprintf(out, "WARNING: %s\n", err)
			}
		}

		scanErr <- nil
	}()

	return scanErr
}

// TODO(fxbug.dev/107801): We can rely less on these fragile regexes if we
// switch to machine-parseable output streams.
func scanForArtifacts(out io.WriteCloser, in io.ReadCloser, artifactPrefix,
	hostArtifactDir string, config *ffxFuzzRunConfig) (chan error, chan []string) {
	// Only replace the directory part of the artifactPrefix, which is
	// guaranteed to be at least "tmp"
	artifactDir := path.Dir(artifactPrefix)

	scanErr := make(chan error, 1)
	artifactsCh := make(chan []string, 1)

	go func() {
		// Propagate EOFs, so that:
		// - The symbolizer terminates properly.
		defer out.Close()
		// - scanForPIDs doesn't block if an early exit occurs later in the chain.
		defer in.Close()

		artifacts := []string{}

		artifactRegex := regexp.MustCompile(`Test unit written to (\S+)`)
		artifactRegexV2 := regexp.MustCompile(`(?:Input saved|Minimized input written) to '([^']+)'`)
		testcaseRegex := regexp.MustCompile(`^Running: (tmp/.+)`)
		testcaseRegexV2 := regexp.MustCompile(`^Running: /tmp/temp_corpus`)
		outputCorpusRegex := regexp.MustCompile(`files found in /tmp/live_corpus`)
		corpusRegex := regexp.MustCompile(`\d+ files found in tmp/`)
		scanner := bufio.NewScanner(in)
		for scanner.Scan() {
			line := scanner.Text()
			if m := artifactRegex.FindStringSubmatch(line); m != nil {
				glog.Infof("Found artifact: %s", m[1])
				if m[1] == "/tmp/result_input" {
					// For v2 fuzzers, this is a hardcoded exact_artifact_path.
					// We want to suppress this line for now, because
					// ClusterFuzz parses it to find the artifact and we don't
					// yet know the "real" artifact filename (until
					// artifactRegexV2 is triggered below)
					line = ""
				} else {
					artifacts = append(artifacts, m[1])
					if hostArtifactDir != "" {
						line = strings.Replace(line, artifactDir, hostArtifactDir, 2)
					}
				}
			} else if m := artifactRegexV2.FindStringSubmatch(line); m != nil {
				glog.Infof("Found v2 artifact: %s", m[1])
				// This is a virtual target path that mimics what libFuzzer
				// would have done. It will be mapped into existence, as
				// necessary, after the fuzzer completes.
				artifactPrefix = "tmp/"
				artifactPath := path.Join(artifactPrefix, filepath.Base(m[1]))
				artifacts = append(artifacts, artifactPath)
				if hostArtifactDir != "" {
					// All paths should become host paths
					artifactPrefix = hostArtifactDir + "/"
					artifactPath = filepath.Join(hostArtifactDir, filepath.Base(m[1]))
				}

				// Emit a libFuzzer-style artifact line now that we know the full artifact name
				line = fmt.Sprintf("artifact_prefix='%s'; Test unit written to %s",
					artifactPrefix, artifactPath)
			} else if m := testcaseRegex.FindStringSubmatch(line); m != nil {
				// The ClusterFuzz integration test for the repro workflow
				// expects to see the path of the passed testcase echoed back
				// in libFuzzer's output, so we can't leak the fact that we've
				// translated paths behind the scenes. Since that test is the
				// only place that relies on this behavior, this rewriting may
				// possibly be removed in the future if the test changes.
				line = "Running: data/" + strings.TrimPrefix(m[1], "tmp/")
			} else if m := testcaseRegexV2.FindStringSubmatch(line); m != nil {
				// See above, with different logic for CFF
				line = "Running: " + config.testcasePath
			} else if m := outputCorpusRegex.FindStringSubmatch(line); m != nil {
				// This is just to pass ClusterFuzz integration tests
				if config.outputCorpus != "" {
					line = strings.Replace(line, "/tmp/live_corpus", config.outputCorpus, 1)
				}
			} else if m := corpusRegex.FindStringSubmatch(line); m != nil {
				// As above, this is just to pass ClusterFuzz integration tests.
				line = strings.Replace(line, "tmp/", "data/", 1)
			}

			if _, err := io.WriteString(out, line+"\n"); err != nil {
				scanErr <- fmt.Errorf("error writing: %s", err)
				return
			}
		}

		scanErr <- scanner.Err()
		artifactsCh <- artifacts
	}()

	return scanErr, artifactsCh
}

// PrepareFuzzer ensures the named fuzzer is ready to be used on the Instance.
// This must be called before running the fuzzer or exchanging any data with
// the fuzzer.
func (f *Fuzzer) Prepare(conn Connector) error {
	var dataPath string

	if f.isV2() {
		// Stop any existing session that may have gotten stuck, and reset the
		// fuzzer's options and live corpus.
		if _, err := conn.FfxRun("", "fuzz", "stop", f.url); err != nil {
			return fmt.Errorf("error ensuring fuzzer is stopped: %s", err)
		}

		dataPath = ""
	} else {

		// TODO(fxbug.dev/61521): We shouldn't rely on executing these commands
		if err := conn.Command("pkgctl", "resolve", f.pkgUrl).Run(); err != nil {
			return fmt.Errorf("error resolving fuzzer package %q: %s", f.pkgUrl, err)
		}

		// Kill any prior running instances of this fuzzer that may have gotten stuck
		if err := conn.Command("killall", f.pkgUrl).Run(); err != nil {
			// `killall` will return -1 if no matching task is found, but this is fine
			if cmderr, ok := err.(*InstanceCmdError); !ok || cmderr.ReturnCode != 255 {
				return fmt.Errorf("error killing any existing instances of %q: %s", f.pkgUrl, err)
			}
		}

		dataPath = "tmp/*"
	}

	// Clear any persistent data in the fuzzer's namespace, resetting its state
	if err := conn.RmDir(f.AbsPath(dataPath)); err != nil {
		return fmt.Errorf("error clearing fuzzer data namespace %q: %s", dataPath, err)
	}

	return nil
}

// Mapping from libFuzzer args -> CFF args
// Essentially the inverse of the mapping in LibFuzzerRunner::AddArgs() in:
// https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/fuzzing/libfuzzer/runner.cc
// Defaults are from:
// https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/fuzzing/common/options.inc
type cffOption struct {
	name         string
	unit         string
	defaultValue string
}

var optionMapping = map[string]*cffOption{
	"runs":                     {"runs", "count", "0"},
	"max_total_time":           {"max_total_time", "time", "0s"},
	"seed":                     {"seed", "int", "0"},
	"max_len":                  {"max_input_size", "byte", "1mb"},
	"mutate_depth":             {"mutation_depth", "int", "5"},
	"detect_leaks":             {"detect_leaks", "bool", "false"},
	"timeout":                  {"run_limit", "time", "1200s"},
	"malloc_limit_mb":          {"malloc_limit", "mb", "2gb"},
	"rss_limit_mb":             {"oom_limit", "mb", "2gb"},
	"purge_allocator_interval": {"purge_interval", "time", "1s"},
	"print_final_stats":        {"print_final_stats", "bool", "false"},
	"use_value_profile":        {"use_value_profile", "bool", "false"},

	// Options that are not supported, but safe to ignore because they are
	// handled in other ways.
	"artifact_prefix":     nil,
	"exact_artifact_path": nil,
	"minimize_crash":      nil,
	"merge":               nil,
	"merge_control_file":  nil,
	"jobs":                nil,

	// TODO(fxbug.dev/108878): Translate this once ffx fuzz supports it
	"dict": nil,
}

func (f *Fuzzer) setCFFOptions(conn Connector) error {
	// First check for any unsupported libFuzzer options
	for opt, val := range f.options {
		// Only the default value for -jobs is supported
		if opt == "jobs" && val != "0" {
			return fmt.Errorf("only -jobs=0 is supported, not %q", val)
		}

		if _, ok := optionMapping[opt]; !ok {
			return fmt.Errorf("unsupported libFuzzer option: -%s=%s", opt, val)
		}
	}

	for libFuzzerOpt, cffOpt := range optionMapping {
		if cffOpt == nil {
			// This option is handled/emulated elsewhere
			continue
		}

		val, ok := f.options[libFuzzerOpt]
		if ok {
			// Add units as necessary
			if cffOpt.unit == "mb" {
				val = val + "mb"
			} else if cffOpt.unit == "byte" {
				val = val + "b"
			} else if cffOpt.unit == "time" {
				val = val + "s"
			}
		} else {
			// Still set the value to its default, since they are persisted
			// between subsequent runs
			val = cffOpt.defaultValue
		}

		if _, err := conn.FfxRun("", "fuzz", "set", f.url, cffOpt.name, val); err != nil {
			return fmt.Errorf("error setting option %s=%s: %s", cffOpt, val, err)
		}
	}
	return nil
}

// Mark the given virtual target directory as needing to be updated with an
// explicit fetch from on-target corpus the next time `get_data` is called.
func (f *Fuzzer) markOutputCorpus(conn Connector, targetDir string) error {
	// Note: We have to make a temp directory and not just a temp file here
	// so we can control the entire filename.
	markerDir, err := os.MkdirTemp("", "undercoat_tmp")
	if err != nil {
		return fmt.Errorf("error creating tmpdir: %s", err)
	}
	defer os.RemoveAll(markerDir)
	marker := filepath.Join(markerDir, liveCorpusMarkerName)
	// In theory, the connector could reconstruct the URL using the
	// cache namespace, but this lets it be less tightly coupled
	if err := os.WriteFile(marker, []byte(f.url), 0o600); err != nil {
		return fmt.Errorf("error writing marker file: %s", err)
	}
	return conn.Put(marker, f.AbsPath(targetDir))
}

type ffxFuzzRunConfig struct {
	// Note: all paths here, other than outputDir, are target paths

	command   string
	args      []string
	outputDir string

	inputCorpora []string
	outputCorpus string

	// This is needed only for libFuzzer output rewriting
	testcasePath string
}

func (f *Fuzzer) parseArgsForFfx(conn Connector) (*ffxFuzzRunConfig, error) {
	// TODO(fxbug.dev/110231): overnet fails when given too much data on a `zx.socket`. This can be
	// observed with targets that emit syslogs on every iteration. Since libFuzzer prints all relevant
	// info to stderr, workaround this issue by disabling stdout and syslog.
	config := ffxFuzzRunConfig{
		args: []string{f.url, "--no-stdout", "--no-syslog"},
	}

	// Split args into files and directories
	var fileArgs []string
	var dirArgs []string
	for _, arg := range f.args {
		if isDir, err := conn.IsDir(f.AbsPath(arg)); err != nil {
			return nil, fmt.Errorf("error stat-ing input: %s", err)
		} else if isDir {
			dirArgs = append(dirArgs, arg)
		} else {
			fileArgs = append(fileArgs, arg)
		}
	}

	// TODO(fxbug.dev/108878): Support cleanse

	// Make sure we weren't passed conflicting args
	minimizeRequested := f.options["minimize_crash"] == "1"
	mergeRequested := f.options["merge"] == "1"
	if minimizeRequested && mergeRequested {
		return nil, fmt.Errorf("only one of minimize and merge can be selected: %s", f.args)
	} else if minimizeRequested && len(fileArgs) == 0 {
		return nil, fmt.Errorf("minimize requires a testcase to be passed: %s", f.args)
	} else if mergeRequested && len(dirArgs) == 0 {
		return nil, fmt.Errorf("merge requires corpus directories to be passed: %s", f.args)
	}

	// Figure out what command to run (based on arg types and options passed)
	if len(fileArgs) > 0 && len(dirArgs) > 0 {
		return nil, fmt.Errorf("mixing file and directory inputs is not supported: %s", f.args)
	} else if len(fileArgs) > 0 {
		config.testcasePath = fileArgs[0]
		if minimizeRequested {
			config.command = "minimize"
		} else {
			config.command = "try"
		}
		if len(fileArgs) > 1 {
			glog.Warningf("%q only supports one file; ignoring extra args: %s",
				config.command, f.args)
		}
		config.args = append(config.args, f.AbsPath(config.testcasePath))
	} else {
		if len(dirArgs) > 0 {
			config.outputCorpus = dirArgs[0]
		}
		if len(dirArgs) > 1 {
			config.inputCorpora = dirArgs[1:]
		}
		if mergeRequested {
			if len(dirArgs) < 2 {
				return nil, fmt.Errorf("merge requires at least 2 directory args: %s", f.args)
			}
			config.command = "merge"
		} else {
			config.command = "run"
		}
	}

	return &config, nil
}

// Run the fuzzer, sending symbolized output to `out` and returning a list of
// any referenced artifacts (e.g. crashes) as absolute paths. If provided,
// `hostArtifactDir` will be used to transparently rewrite artifact_prefix
// references in artifact paths in the output log.
func (f *Fuzzer) Run(conn Connector, out io.Writer, hostArtifactDir string) ([]string, error) {
	if f.options == nil {
		return nil, fmt.Errorf("Run called on Fuzzer before Parse")
	}

	var cmd InstanceCmd
	var ffxConfig *ffxFuzzRunConfig
	if f.isV2() {
		config, err := f.parseArgsForFfx(conn)
		if err != nil {
			return nil, fmt.Errorf("error translating args: %s", err)
		}

		if err := f.setCFFOptions(conn); err != nil {
			return nil, fmt.Errorf("error translating options: %s", err)
		}

		tempDir, err := os.MkdirTemp("", "undercoat-ffx-output-")
		if err != nil {
			return nil, fmt.Errorf("Error creating temporary ffx output directory: %s", err)
		}
		defer os.RemoveAll(tempDir)
		config.outputDir = tempDir

		// Push any input corpora over first
		start := time.Now()
		for _, inputDir := range config.inputCorpora {
			addCmd := []string{"fuzz", "add", f.url, f.AbsPath(inputDir)}
			if _, err := conn.FfxRun("", addCmd...); err != nil {
				return nil, fmt.Errorf("error adding input corpus %q: %s", inputDir, err)
			}
		}
		if len(config.inputCorpora) > 0 {
			glog.Infof("Pushed input corpora in %s", time.Since(start))
		}

		// Mark any output corpus dir as being special, so it can be fetched as
		// necessary later (except for Merge, which auto-fetches the result;
		// for more info, see the code that moves the merge corpus into place
		// below).
		if config.outputCorpus != "" && config.command != "merge" {
			if err := f.markOutputCorpus(conn, config.outputCorpus); err != nil {
				return nil, fmt.Errorf("error marking output corpus dir: %s", err)
			}
		}

		ffxArgs := append([]string{"fuzz", config.command}, config.args...)
		ffxCmd, err := conn.FfxCommand(config.outputDir, ffxArgs...)
		if err != nil {
			return nil, fmt.Errorf("error constructing ffx call: %s", err)
		}

		ffxConfig = config
		cmd = &FfxInstanceCmd{ffxCmd}
	} else {
		// Ensure artifact_prefix will be writable, and fall back to default if not
		// specified
		if artPrefix, ok := f.options["artifact_prefix"]; ok {
			if !strings.HasPrefix(artPrefix, "tmp/") {
				return nil, fmt.Errorf("artifact_prefix not in mutable namespace: %q",
					artPrefix)
			}
		} else {
			f.options["artifact_prefix"] = "tmp/"
		}

		cmdline := []string{f.url}
		for k, v := range f.options {
			cmdline = append(cmdline, fmt.Sprintf("-%s=%s", k, v))
		}
		for _, arg := range f.args {
			cmdline = append(cmdline, arg)
		}
		cmd = conn.Command("run", cmdline...)
	}

	// The overall flow of fuzzer output data is as follows:
	// fuzzer -> scanForPIDs -> scanForArtifacts -> symbolizer -> out
	// In addition to the log lines and EOF (on fuzzer exit) that pass from
	// left to right, an EOF will also be propagated backwards in the case that
	// any of the intermediate steps exits abnormally, so as not to leave
	// blocking writes from earlier stages in a hung state.

	fuzzerOutput, err := cmd.StdoutPipe()
	if err != nil {
		return nil, fmt.Errorf("error getting fuzzer stdout: %s", err)
	}
	if !f.isV2() {
		// ffx fuzz combines stdout and stderr into stdout for us already, but
		// for v1 fuzzers we need to do this ourself
		fuzzerStderr, err := cmd.StderrPipe()
		if err != nil {
			return nil, fmt.Errorf("error getting fuzzer stderr: %s", err)
		}
		fuzzerOutput = parallelMultiReader(fuzzerOutput, fuzzerStderr)
	}

	if err := cmd.Start(); err != nil {
		return nil, err
	}

	fromPIDScanner, toArtifactScanner := io.Pipe()

	// Start goroutine to scan for PID and insert syslog
	pidScanErr := scanForPIDs(conn, toArtifactScanner, fuzzerOutput)

	fromArtifactScanner, toSymbolizer := io.Pipe()

	// Start goroutine to check/rewrite artifact paths
	artifactScanErr, artifactCh := scanForArtifacts(toSymbolizer, fromPIDScanner,
		f.options["artifact_prefix"], hostArtifactDir, ffxConfig)

	// Start symbolizer goroutine, connected to the output
	symErr := make(chan error, 1)
	go func(in io.ReadCloser) {
		symErr <- f.build.Symbolize(in, out)
	}(fromArtifactScanner)

	// Check for any errors in the goroutines
	if err := <-symErr; err != nil {
		return nil, fmt.Errorf("failed during symbolization: %s", err)
	}
	if err := <-pidScanErr; err != nil {
		return nil, fmt.Errorf("failed during PID scanning: %s", err)
	}
	if err := <-artifactScanErr; err != nil {
		return nil, fmt.Errorf("failed during artifact scanning: %s", err)
	}

	// We can Wait now that all reads from the fuzzerOutput pipe have completed.
	err = cmd.Wait()
	glog.Infof("Fuzzer run has completed")

	// Check the returncode (though this might be modified
	// by libfuzzer args and thus be unreliable)
	if cmderr, ok := err.(*InstanceCmdError); ok {
		found := false
		for _, code := range expectedFuzzerReturnCodes {
			if cmderr.ReturnCode == code {
				found = true
				break
			}
		}
		if !found {
			return nil, fmt.Errorf("unexpected return code: %s", err)
		}
	} else if cmderr, ok := err.(*exec.ExitError); ok {
		// ffx fuzz does not propagate libFuzzer errors, so any non-zero code
		// indicates a problem
		glog.Warningf("ffx stderr: %s", cmderr.Stderr)
		return nil, fmt.Errorf("unexpected return code: %s", err)
	} else if err != nil {
		return nil, fmt.Errorf("failed during wait: %s", err)
	}

	var artifacts []string
	for _, artifact := range <-artifactCh {
		if f.isV2() {
			// ffx has already copied the file to the host
			ffxArtifactDir := filepath.Join(ffxConfig.outputDir, "artifacts")
			hostArtifactPath := filepath.Join(ffxArtifactDir, path.Base(artifact))

			// Emulate exact_artifact_prefix by renaming the artifact
			if f.options["exact_artifact_path"] != "" {
				artifact = f.options["exact_artifact_path"]
				newHostArtifactPath := filepath.Join(ffxArtifactDir, path.Base(artifact))
				if err := os.Rename(hostArtifactPath, newHostArtifactPath); err != nil {
					return nil, fmt.Errorf("error renaming artifact: %s", err)
				}
				hostArtifactPath = newHostArtifactPath
			}

			// Map virtual target paths
			if err := conn.Put(hostArtifactPath, f.AbsPath(path.Dir(artifact))); err != nil {
				return nil, fmt.Errorf("error caching artifact: %s", err)
			}
		}
		artifacts = append(artifacts, f.AbsPath(artifact))
	}

	// In the Merge case, the output corpus is auto-fetched, so to avoid
	// redundant work we just copy it into place in the cache now.
	// TODO(fxbug.dev/108877): Avoid the extra copy here once we can override
	// the corpus output directory.
	if f.isV2() && ffxConfig.command == "merge" {
		fetchedDir := filepath.Join(ffxConfig.outputDir, "corpus")
		if err := conn.Put(fetchedDir+"/*", f.AbsPath(ffxConfig.outputCorpus)); err != nil {
			return nil, fmt.Errorf("error copying merged corpus into place: %s", err)
		}
	}

	return artifacts, nil
}
