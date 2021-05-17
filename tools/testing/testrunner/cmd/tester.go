// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"encoding/hex"
	"fmt"
	"io"
	"io/ioutil"
	"math"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/debug/elflib"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/serial"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"go.fuchsia.dev/fuchsia/tools/testing/testrunner/constants"
	"golang.org/x/crypto/ssh"
)

const (
	// A test output directory within persistent storage.
	dataOutputDir = "/data/infra/testrunner"

	// TODO(fxb/73171): Fix this path.
	// The output data directory for component v2 tests.
	dataOutputDirV2 = "/tmp/test_manager:0/children/debug_data:0/data"

	// Various tools for running tests.
	runtestsName         = "runtests"
	runTestComponentName = "run-test-component"
	runTestSuiteName     = "run-test-suite"

	componentV2Suffix = ".cm"

	// Returned by both run-test-component and run-test-suite to indicate the
	// test timed out.
	timeoutExitCode = 21

	// Printed to the serial console when ready to accept user input.
	serialConsoleCursor = "\n$"

	llvmProfileEnvKey    = "LLVM_PROFILE_FILE"
	llvmProfileExtension = ".profraw"
	llvmProfileSinkType  = "llvm-profile"
)

type timeoutError struct {
	timeout time.Duration
}

func (e *timeoutError) Error() string {
	return fmt.Sprintf("test killed because timeout reached (%v)", e.timeout)
}

// For testability
type cmdRunner interface {
	Run(ctx context.Context, command []string, stdout, stderr io.Writer) error
}

// For testability
var newRunner = func(dir string, env []string) cmdRunner {
	return &subprocess.Runner{Dir: dir, Env: env}
}

// For testability
type sshClient interface {
	Close()
	Reconnect(ctx context.Context) error
	Run(ctx context.Context, command []string, stdout, stderr io.Writer) error
}

// For testability
type dataSinkCopier interface {
	GetReferences(remoteDir string) (map[string]runtests.DataSinkReference, error)
	Copy(sinks []runtests.DataSinkReference, localDir string) (runtests.DataSinkMap, error)
	Reconnect() error
	Close() error
}

// For testability
type serialClient interface {
	runDiagnostics(ctx context.Context) error
}

// subprocessTester executes tests in local subprocesses.
type subprocessTester struct {
	env               []string
	dir               string
	perTestTimeout    time.Duration
	localOutputDir    string
	getModuleBuildIDs func(string) ([]string, error)
}

func getModuleBuildIDs(test string) ([]string, error) {
	f, err := os.Open(test)
	if err != nil {
		return nil, err
	}
	buildIDs, err := elflib.GetBuildIDs(filepath.Base(test), f)
	if err != nil {
		return nil, err
	}
	var asStrings []string
	for _, id := range buildIDs {
		asStrings = append(asStrings, hex.EncodeToString(id))
	}
	return asStrings, nil
}

// NewSubprocessTester returns a SubprocessTester that can execute tests
// locally with a given working directory and environment.
func newSubprocessTester(dir string, env []string, localOutputDir string, perTestTimeout time.Duration) *subprocessTester {
	return &subprocessTester{
		dir:               dir,
		env:               env,
		perTestTimeout:    perTestTimeout,
		localOutputDir:    localOutputDir,
		getModuleBuildIDs: getModuleBuildIDs,
	}
}

func (t *subprocessTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer, outDir string) (runtests.DataSinkReference, error) {
	sinkRef := runtests.DataSinkReference{}
	if test.Path == "" {
		return sinkRef, fmt.Errorf("test %q has no `path` set", test.Name)
	}
	// Some tests read testOutDirEnvKey so ensure they get their own output dir.
	if err := os.MkdirAll(outDir, 0770); err != nil {
		return sinkRef, err
	}

	// Might as well emit any profiles directly to the output directory.
	// TODO(fxbug.dev/61208): until this is resolved, we make the assumption
	// that the binaries are statically linked and will only produce one
	// profile on execution. Once build IDs are embedded in profiles
	// automatically, we can switch to a more flexible scheme where, say,
	// we set
	// LLVM_PROFILE_FILE=<output dir>/<test-specific namsepace>/%p.profraw
	// and then record any .profraw file written to that directory as an
	// emitted profile.
	profileRel := filepath.Join(llvmProfileSinkType, test.Path+llvmProfileExtension)
	profileAbs := filepath.Join(t.localOutputDir, profileRel)
	os.MkdirAll(filepath.Dir(profileAbs), os.ModePerm)

	r := newRunner(t.dir, append(
		t.env,
		fmt.Sprintf("%s=%s", testOutDirEnvKey, outDir),
		// When host-side tests are instrumented for profiling, executing
		// them will write a profile to the location under this environment variable.
		fmt.Sprintf("%s=%s", llvmProfileEnvKey, profileAbs),
	))
	if t.perTestTimeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, t.perTestTimeout)
		defer cancel()
	}
	err := r.Run(ctx, []string{test.Path}, stdout, stderr)
	if err == context.DeadlineExceeded {
		err = &timeoutError{t.perTestTimeout}
	}

	if exists, profileErr := osmisc.FileExists(profileAbs); profileErr != nil {
		logger.Errorf(ctx, "unable to determine whether a profile was emitted: %v", profileErr)
	} else if exists {
		// TODO(fxbug.dev/61208): delete determination of build IDs once
		// profiles embed this information.
		var buildIDs []string
		buildIDs, profileErr = t.getModuleBuildIDs(test.Path)
		if profileErr == nil {
			sinkRef.Sinks = runtests.DataSinkMap{
				llvmProfileSinkType: []runtests.DataSink{
					{
						Name:     filepath.Base(profileRel),
						File:     profileRel,
						BuildIDs: buildIDs,
					},
				},
			}
		} else {
			logger.Warningf(ctx, "failed to read module build IDs from %q", test.Path)
		}
	}
	return sinkRef, err
}

func (t *subprocessTester) EnsureSinks(ctx context.Context, sinkRefs []runtests.DataSinkReference, _ *testOutputs) error {
	// Nothing to actually copy; if any profiles were emitted, they would have
	// been written directly to the output directory. We verify here that all
	// recorded data sinks are actually present.
	numSinks := 0
	for _, ref := range sinkRefs {
		for _, sinks := range ref.Sinks {
			for _, sink := range sinks {
				abs := filepath.Join(t.localOutputDir, sink.File)
				exists, err := osmisc.FileExists(abs)
				if err != nil {
					return fmt.Errorf("unable to determine if local data sink %q exists: %v", sink.File, err)
				} else if !exists {
					return fmt.Errorf("expected a local data sink %q, but no such file exists", sink.File)
				}
				numSinks++
			}
		}
	}
	if numSinks > 0 {
		logger.Debugf(ctx, "local data sinks present: %d", numSinks)
	}
	return nil
}

func (t *subprocessTester) RunSnapshot(_ context.Context, _ string) error {
	return nil
}

func (t *subprocessTester) Close() error {
	return nil
}

type serialSocket struct {
	socketPath string
}

func (s *serialSocket) runDiagnostics(ctx context.Context) error {
	if s.socketPath == "" {
		return fmt.Errorf("serialSocketPath not set")
	}
	socket, err := serial.NewSocket(ctx, s.socketPath)
	if err != nil {
		return fmt.Errorf("newSerialSocket failed: %v", err)
	}
	defer socket.Close()
	return serial.RunDiagnostics(ctx, socket)
}

// fuchsiaSSHTester executes fuchsia tests over an SSH connection.
type fuchsiaSSHTester struct {
	client                      sshClient
	copier                      dataSinkCopier
	useRuntests                 bool
	localOutputDir              string
	perTestTimeout              time.Duration
	connectionErrorRetryBackoff retry.Backoff
	serialSocket                serialClient
}

// newFuchsiaSSHTester returns a fuchsiaSSHTester associated to a fuchsia
// instance of given nodename, the private key paired with an authorized one
// and the directive of whether `runtests` should be used to execute the test.
func newFuchsiaSSHTester(ctx context.Context, addr net.IPAddr, sshKeyFile, localOutputDir, serialSocketPath string, useRuntests bool, perTestTimeout time.Duration) (*fuchsiaSSHTester, error) {
	key, err := ioutil.ReadFile(sshKeyFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read SSH key file: %w", err)
	}
	config, err := sshutil.DefaultSSHConfig(key)
	if err != nil {
		return nil, fmt.Errorf("failed to create an SSH client config: %w", err)
	}

	client, err := sshutil.NewClient(
		ctx,
		sshutil.ConstantAddrResolver{
			Addr: &net.TCPAddr{
				IP:   addr.IP,
				Port: sshutil.SSHPort,
				Zone: addr.Zone,
			},
		},
		config,
		sshutil.DefaultConnectBackoff(),
	)
	if err != nil {
		return nil, fmt.Errorf("failed to establish an SSH connection: %w", err)
	}
	copier, err := runtests.NewDataSinkCopier(client)
	if err != nil {
		return nil, err
	}
	return &fuchsiaSSHTester{
		client:                      client,
		copier:                      copier,
		useRuntests:                 useRuntests,
		localOutputDir:              localOutputDir,
		perTestTimeout:              perTestTimeout,
		connectionErrorRetryBackoff: retry.NewConstantBackoff(time.Second),
		serialSocket:                &serialSocket{serialSocketPath},
	}, nil
}

func (t *fuchsiaSSHTester) reconnect(ctx context.Context) error {
	if err := t.client.Reconnect(ctx); err != nil {
		return fmt.Errorf("failed to reestablish SSH connection: %w", err)
	}
	if err := t.copier.Reconnect(); err != nil {
		return fmt.Errorf("failed to reconnect data sink copier: %w", err)
	}
	return nil
}

func (t *fuchsiaSSHTester) isTimeoutError(test testsharder.Test, err error) bool {
	if t.perTestTimeout <= 0 {
		return false
	}
	if exitErr, ok := err.(*ssh.ExitError); ok {
		return exitErr.Waitmsg.ExitStatus() == timeoutExitCode
	}
	return false
}

func (t *fuchsiaSSHTester) runSSHCommandWithRetry(ctx context.Context, command []string, stdout, stderr io.Writer) error {
	var cmdErr error
	const maxReconnectAttempts = 3
	retry.Retry(ctx, retry.WithMaxAttempts(t.connectionErrorRetryBackoff, maxReconnectAttempts), func() error {
		cmdErr = t.client.Run(ctx, command, stdout, stderr)
		if sshutil.IsConnectionError(cmdErr) {
			logger.Errorf(ctx, "attempting to reconnect over SSH after error: %v", cmdErr)
			if err := t.reconnect(ctx); err != nil {
				logger.Errorf(ctx, "%s: %v", constants.FailedToReconnectMsg, err)
				// If we fail to reconnect, continuing is likely hopeless.
				return nil
			}
			// Return non-ConnectionError because code in main.go will exit early if
			// it sees that. Since reconnection succeeded, we don't want that.
			// TODO(garymm): Clean this up; have main.go do its own connection recovery between tests.
			cmdErr = fmt.Errorf("%v", cmdErr)
			return cmdErr
		}
		// Not a connection error -> command passed or failed -> break retry loop.
		return nil
	}, nil)
	return cmdErr
}

// Return this error when the test is skipped.
type TestSkippedError struct{}

func (e *TestSkippedError) Error() string {
	return "test skipped"
}

func isTestSkippedErr(err error) bool {
	_, ok := err.(*TestSkippedError)
	return ok
}

// Test runs a test over SSH.
func (t *fuchsiaSSHTester) Test(ctx context.Context, test testsharder.Test, stdout io.Writer, stderr io.Writer, _ string) (runtests.DataSinkReference, error) {
	sinks := runtests.DataSinkReference{}
	command, err := commandForTest(&test, t.useRuntests, dataOutputDir, t.perTestTimeout)
	if err != nil {
		return sinks, err
	}
	testErr := t.runSSHCommandWithRetry(ctx, command, stdout, stderr)

	if sshutil.IsConnectionError(testErr) {
		if err := t.serialSocket.runDiagnostics(ctx); err != nil {
			logger.Warningf(ctx, "failed to run serial diagnostics: %v", err)
		}
		return sinks, testErr
	}

	if t.isTimeoutError(test, testErr) {
		testErr = &timeoutError{t.perTestTimeout}
	}

	var sinkErr error
	if t.useRuntests && !strings.HasSuffix(test.PackageURL, componentV2Suffix) {
		startTime := time.Now()
		var sinksPerTest map[string]runtests.DataSinkReference
		if sinksPerTest, sinkErr = t.copier.GetReferences(dataOutputDir); sinkErr != nil {
			logger.Errorf(ctx, "failed to determine data sinks for test %q: %v", test.Name, sinkErr)
		} else {
			sinks = sinksPerTest[test.Name]
		}
		duration := time.Now().Sub(startTime)
		if sinks.Size() > 0 {
			logger.Debugf(ctx, "%d data sinks found in %v", sinks.Size(), duration)
		}
	}

	if testErr == nil {
		return sinks, sinkErr
	}
	return sinks, testErr
}

func (t *fuchsiaSSHTester) EnsureSinks(ctx context.Context, sinkRefs []runtests.DataSinkReference, outputs *testOutputs) error {
	// Collect v2 references.
	v2Sinks, err := t.copier.GetReferences(dataOutputDirV2)
	if err != nil {
		// If we fail to get v2 sinks, just log the error but continue to copy v1 sinks.
		logger.Errorf(ctx, "failed to determine data sinks for v2 tests: %v", err)
	}
	var v2SinkRefs []runtests.DataSinkReference
	for _, ref := range v2Sinks {
		v2SinkRefs = append(v2SinkRefs, ref)
	}
	if len(v2SinkRefs) > 0 {
		if err := t.copySinks(ctx, v2SinkRefs, filepath.Join(t.localOutputDir, "v2")); err != nil {
			return err
		}
		outputs.updateDataSinks(v2Sinks, "v2")
	}
	return t.copySinks(ctx, sinkRefs, t.localOutputDir)
}

func (t *fuchsiaSSHTester) copySinks(ctx context.Context, sinkRefs []runtests.DataSinkReference, localOutputDir string) error {
	startTime := time.Now()
	sinkMap, err := t.copier.Copy(sinkRefs, localOutputDir)
	if err != nil {
		return fmt.Errorf("failed to copy data sinks off target: %v", err)
	}
	copyDuration := time.Now().Sub(startTime)
	sinkRef := runtests.DataSinkReference{Sinks: sinkMap}
	numSinks := sinkRef.Size()
	if numSinks > 0 {
		logger.Debugf(ctx, "copied %d data sinks in %v", numSinks, copyDuration)
	}
	return nil
}

// RunSnapshot runs `snapshot` on the device.
func (t *fuchsiaSSHTester) RunSnapshot(ctx context.Context, snapshotFile string) error {
	if snapshotFile == "" {
		return nil
	}
	snapshotOutFile, err := osmisc.CreateFile(filepath.Join(t.localOutputDir, snapshotFile))
	if err != nil {
		return fmt.Errorf("failed to create snapshot output file: %w", err)
	}
	defer snapshotOutFile.Close()
	startTime := time.Now()
	err = t.runSSHCommandWithRetry(ctx, []string{"/bin/snapshot"}, snapshotOutFile, os.Stderr)
	if err != nil {
		logger.Errorf(ctx, "%s: %v", constants.FailedToRunSnapshotMsg, err)
	}
	logger.Debugf(ctx, "ran snapshot in %v", time.Now().Sub(startTime))
	return err
}

// Close terminates the underlying SSH connection. The object is no longer
// usable after calling this method.
func (t *fuchsiaSSHTester) Close() error {
	defer t.client.Close()
	return t.copier.Close()
}

// FuchsiaSerialTester executes fuchsia tests over serial.
type fuchsiaSerialTester struct {
	socket         io.ReadWriteCloser
	perTestTimeout time.Duration
	localOutputDir string
}

func newFuchsiaSerialTester(ctx context.Context, serialSocketPath string, perTestTimeout time.Duration) (*fuchsiaSerialTester, error) {
	socket, err := serial.NewSocket(ctx, serialSocketPath)
	if err != nil {
		return nil, err
	}

	return &fuchsiaSerialTester{
		socket:         socket,
		perTestTimeout: perTestTimeout,
	}, nil
}

// Exposed for testability.
var newTestStartedContext = func(ctx context.Context) (context.Context, context.CancelFunc) {
	return context.WithTimeout(ctx, 5*time.Second)
}

// lastWriteSaver is an io.Writer that saves the bytes written in the last Write().
type lastWriteSaver struct {
	buf []byte
}

func (w *lastWriteSaver) Write(p []byte) (int, error) {
	w.buf = make([]byte, len(p))
	copy(w.buf, p)
	return len(p), nil
}

// parseOutKernelReader is an io.Reader that reads from the underlying reader
// everything not pertaining to a kernel log. A kernel log is distinguished by
// a line that starts with the timestamp represented as a float inside brackets.
type parseOutKernelReader struct {
	reader io.Reader
	// lineStart stores the last characters read from a Read() block if it
	// ended with a truncated line. This will be prepended to the next Read() block.
	lineStart  string
	reachedEOF bool
}

func (r *parseOutKernelReader) Read(buf []byte) (int, error) {
	var err error
	if r.reachedEOF {
		bytesToRead := int(math.Min(float64(len(buf)), float64(len(r.lineStart))))
		copy(buf, []byte(r.lineStart[:bytesToRead]))
		r.lineStart = r.lineStart[bytesToRead:]
		if bytesToRead > 0 {
			err = nil
		} else {
			err = io.EOF
		}
		return bytesToRead, err
	}
	bytesRead := 0
	for bytesRead < len(buf) {
		bytesLeftToRead := len(buf) - bytesRead
		b := make([]byte, bytesLeftToRead)
		var n int
		n, err = r.reader.Read(b)
		if err != nil && err != io.EOF {
			break
		}
		// readBlock contains everything stored in lineStart (bytes read from the
		// underlying reader but not processed or read by this reader yet) along
		// with the new bytes just read. Because readBlock contains lineStart, its
		// length will likely be greater than bytesLeftToRead. However, it is
		// necessary to read more bytes in the case that lineStart contains a long
		// truncated kernel log and we need to keep reading more bytes until we get
		// to the end of the line so we can discard it.
		readBlock := r.lineStart + string(b[:n])
		r.lineStart = ""
		lines := strings.Split(readBlock, "\n")
		for i, line := range lines {
			bytesLeftToRead = len(buf) - bytesRead
			isTruncated := i == len(lines)-1
			line = r.lineWithoutKernelLog(line, isTruncated)
			if bytesLeftToRead == 0 {
				// If there are no more bytes left to read, store the rest of the lines
				// into lineStart to be read at the next call to Read().
				r.lineStart += line
				continue
			}
			if len(line) > bytesLeftToRead {
				// If the line is longer than bytesLeftToRead, read as much as possible
				// and store the rest in lineStart.
				copy(buf[bytesRead:], []byte(line[:bytesLeftToRead]))
				r.lineStart = line[bytesLeftToRead:]
				bytesRead += bytesLeftToRead
			} else {
				copy(buf[bytesRead:bytesRead+len(line)], []byte(line))
				bytesRead += len(line)
			}
		}
		if n < len(b) {
			if err == io.EOF {
				r.reachedEOF = true
			}
			if len(r.lineStart) > 0 {
				err = nil
			}
			break
		}
	}
	return bytesRead, err
}

func (r *parseOutKernelReader) lineWithoutKernelLog(line string, isTruncated bool) string {
	containsKernelLog := false
	re := regexp.MustCompile(`\[[0-9]+\.?[0-9]+\]`)
	match := re.FindStringIndex(line)
	if match != nil {
		if isTruncated {
			r.lineStart = line[match[0]:]
		}
		// The new line to add to bytes read contains everything in the line up to
		// the bracket indicating the kernel log.
		line = line[:match[0]]
		containsKernelLog = true
	} else if isTruncated {
		// Match the beginning of a possible kernel log timestamp.
		// i.e. `[`, `[123` `[123.4`
		re = regexp.MustCompile(`\[[0-9]*\.?[0-9]*$`)
		match = re.FindStringIndex(line)
		if match != nil {
			r.lineStart = line[match[0]:]
			line = line[:match[0]]
		}
	}
	if !containsKernelLog && !isTruncated {
		line = line + "\n"
	}
	return line
}

func (t *fuchsiaSerialTester) Test(ctx context.Context, test testsharder.Test, stdout, _ io.Writer, _ string) (runtests.DataSinkReference, error) {
	// We don't collect data sinks for serial tests. Just return an empty DataSinkReference.
	sinks := runtests.DataSinkReference{}
	command, err := commandForTest(&test, true, "", t.perTestTimeout)
	if err != nil {
		return sinks, err
	}
	logger.Debugf(ctx, "starting: %v", command)

	// If a single read from the socket includes both the bytes that indicate the test started and the bytes
	// that indicate the test completed, then the startedReader will consume the bytes needed for detecting
	// completion. Thus we save the last read from the socket and replay it when searching for completion.
	lastWrite := &lastWriteSaver{}
	startedReader := iomisc.NewMatchingReader(io.TeeReader(t.socket, lastWrite), [][]byte{[]byte(runtests.StartedSignature + test.Name)})
	for ctx.Err() == nil {
		if err := serial.RunCommands(ctx, t.socket, []serial.Command{{command, 0}}); err != nil {
			return sinks, fmt.Errorf("failed to write to serial socket: %v", err)
		}
		startedCtx, cancel := newTestStartedContext(ctx)
		_, err := iomisc.ReadUntilMatch(startedCtx, startedReader)
		cancel()
		if err == nil {
			break
		}
		logger.Warningf(ctx, "test not started after timeout, retrying")
	}

	if ctx.Err() != nil {
		return sinks, ctx.Err()
	}

	testOutputReader := io.TeeReader(
		// See comment above lastWrite declaration.
		&parseOutKernelReader{reader: io.MultiReader(bytes.NewReader(lastWrite.buf), t.socket)},
		// Writes to stdout as it reads from the above reader.
		stdout)
	success, err := runtests.TestPassed(ctx, testOutputReader, test.Name)

	if err != nil {
		return sinks, err
	} else if !success {
		return sinks, fmt.Errorf("test failed")
	}
	return sinks, nil
}

func (t *fuchsiaSerialTester) EnsureSinks(_ context.Context, _ []runtests.DataSinkReference, _ *testOutputs) error {
	return nil
}

func (t *fuchsiaSerialTester) RunSnapshot(_ context.Context, _ string) error {
	return nil
}

// Close terminates the underlying Serial socket connection. The object is no
// longer usable after calling this method.
func (t *fuchsiaSerialTester) Close() error {
	return t.socket.Close()
}

func commandForTest(test *testsharder.Test, useRuntests bool, remoteOutputDir string, timeout time.Duration) ([]string, error) {
	command := []string{}
	// For v2 coverage data, use run-test-suite instead of runtests and collect the data from the designated dataOutputDirV2 directory.
	if useRuntests && !strings.HasSuffix(test.PackageURL, componentV2Suffix) {
		command = []string{runtestsName}
		if remoteOutputDir != "" {
			command = append(command, "--output", remoteOutputDir)
		}
		if timeout > 0 {
			command = append(command, "-i", fmt.Sprintf("%d", int64(timeout.Seconds())))
		}
		if test.RealmLabel != "" {
			command = append(command, "--realm-label", test.RealmLabel)
		}
		if test.PackageURL != "" {
			command = append(command, test.PackageURL)
		} else {
			command = append(command, test.Path)
		}
	} else if test.PackageURL != "" {
		if strings.HasSuffix(test.PackageURL, componentV2Suffix) {
			command = []string{runTestSuiteName, "--filter-ansi"}
			if test.LogSettings.MaxSeverity != "" {
				command = append(command, "--max-severity-logs", fmt.Sprintf("%s", test.LogSettings.MaxSeverity))
			}
			if test.Parallel != 0 {
				command = append(command, "--parallel", fmt.Sprintf("%d", test.Parallel))
			}
			// TODO(fxbug.dev/49262): Once fixed, combine timeout flag setting for v1 and v2.
			if timeout > 0 {
				command = append(command, "--timeout", fmt.Sprintf("%d", int64(timeout.Seconds())))
			}
		} else {
			command = []string{runTestComponentName}
			if test.LogSettings.MaxSeverity != "" {
				command = append(command, fmt.Sprintf("--max-log-severity=%s", test.LogSettings.MaxSeverity))
			}

			if timeout > 0 {
				command = append(command, fmt.Sprintf("--timeout=%d", int64(timeout.Seconds())))
			}

			// run-test-component supports realm-label but run-test-suite does not
			if test.RealmLabel != "" {
				command = append(command, "--realm-label", test.RealmLabel)
			}
		}
		command = append(command, test.PackageURL)
	} else {
		return nil, fmt.Errorf("PackageURL is not set and useRuntests is false for %q", test.Name)
	}
	return command, nil
}
