// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"

	"github.com/golang/glog"
)

// A Build represents a Fuchsia build, consisting of all the resources needed
// to run a fuzzer on an instance (e.g. a Fuchsia image, fuzzer packages and
// metadata, binary symbols, support utilities, etc.).
type Build interface {
	// Ensures all the needed resources are present and fetches any that are
	// missing. Multiple calls to Prepare should be idempotent for the same
	// Build.
	Prepare() error

	// Returns a fuzzer specified by a `package/binary` name, or an error if it
	// isn't found.
	Fuzzer(name string) (*Fuzzer, error)

	// Returns the absolute host paths for each key.  Each key corresponds to a
	// specific resource provided by the Build.  This abstraction allows for
	// different build types to have different structures.
	Path(keys ...string) ([]string, error)

	// Reads input from `in`, symbolizes it, and writes it back to `out`.
	// Returns on error, or when `in` has no more data to read.  Processing
	// will be streamed, line-by-line.
	// TODO(fxbug.dev/47482): does this belong elsewhere?
	Symbolize(in io.ReadCloser, out io.Writer) error

	// Returns a list of the names of the fuzzers that are available to run
	ListFuzzers() []string
}

// BaseBuild is a simple implementation of the Build interface
type BaseBuild struct {
	Fuzzers map[string]*Fuzzer
	Paths   map[string]string
	IDs     []string
}

// This is stubbed out to allow for test code to replace it
var NewBuild = NewBuildFromEnvironment

// This environment variable is set by the ClusterFuzz build manager
const clusterFuzzBundleDirEnvVar = "FUCHSIA_RESOURCES_DIR"

// Attempt to auto-detect the correct Build type
func NewBuildFromEnvironment() (Build, error) {
	if _, found := os.LookupEnv(clusterFuzzBundleDirEnvVar); found {
		return NewClusterFuzzLegacyBuild()
	}
	return NewLocalFuchsiaBuild()
}

// NewClusterFuzzLegacyBuild will create a BaseBuild with path layouts
// corresponding to the legacy build bundles used by ClusterFuzz's original
// Python integration. Note that these build bundles only support x64.
func NewClusterFuzzLegacyBuild() (Build, error) {
	bundleDir, found := os.LookupEnv(clusterFuzzBundleDirEnvVar)
	if !found {
		return nil, fmt.Errorf("%s not set", clusterFuzzBundleDirEnvVar)
	}

	buildDir := filepath.Join(bundleDir, "build")
	targetDir := filepath.Join(bundleDir, "target", "x64")
	clangDir := filepath.Join(buildDir, "buildtools", "linux-x64", "clang")
	build := &BaseBuild{
		Paths: map[string]string{
			"zbi":             filepath.Join(targetDir, "fuchsia.zbi"),
			"fvm":             filepath.Join(buildDir, "out", "default.zircon", "tools", "fvm"),
			"zbitool":         filepath.Join(buildDir, "out", "default.zircon", "tools", "zbi"),
			"blk":             filepath.Join(targetDir, "fvm.blk"),
			"qemu":            filepath.Join(bundleDir, "qemu-for-fuchsia", "bin", "qemu-system-x86_64"),
			"kernel":          filepath.Join(targetDir, "multiboot.bin"),
			"llvm-symbolizer": filepath.Join(clangDir, "bin", "llvm-symbolizer"),
			"symbolizer":      filepath.Join(buildDir, "zircon", "prebuilt", "downloads", "symbolize", "linux-x64", "symbolize"),
			"fuzzers.json":    filepath.Join(buildDir, "out", "default", "fuzzers.json"),
			"tests.json":      filepath.Join(buildDir, "out", "default", "tests.json"),
			"ffx":             filepath.Join(bundleDir, "ffx"),
		},
		IDs: []string{
			filepath.Join(clangDir, "lib", "debug", ".build_id"),
			filepath.Join(buildDir, "out", "default", ".build-id"),
			filepath.Join(buildDir, "out", "default.zircon", ".build-id"),
		},
	}
	if err := build.LoadFuzzers(); err != nil {
		return nil, err
	}

	return build, nil
}

var Platforms = map[string]string{
	"linux":  "linux",
	"darwin": "mac",
}

var Archs = map[string]struct {
	Binary string
	Kernel string
}{
	"x64":   {"qemu-system-x86_64", "multiboot.bin"},
	"arm64": {"qemu-system-aarch64", "qemu-boot-shim.bin"},
}

var hostDir = map[string]string{"arm64": "host_arm64", "amd64": "host_x64"}[runtime.GOARCH]

// NewLocalFuchsiaBuild will create a BaseBuild with path layouts corresponding
// to a local Fuchsia checkout
func NewLocalFuchsiaBuild() (Build, error) {
	fuchsiaDir := os.Getenv("FUCHSIA_DIR")
	if fuchsiaDir == "" {
		// Fall back to relative path from this file
		fuchsiaDir = filepath.Join("..", "..")
	}

	fxBuildDir := filepath.Join(fuchsiaDir, ".fx-build-dir")
	contents, err := os.ReadFile(fxBuildDir)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %s", fxBuildDir, err)
	}

	buildDir := strings.TrimSpace(string(contents))
	if !filepath.IsAbs(buildDir) {
		buildDir = filepath.Join(fuchsiaDir, buildDir)
	}
	prebuiltDir := filepath.Join(fuchsiaDir, "prebuilt")

	platform, ok := Platforms[runtime.GOOS]
	if !ok {
		return nil, fmt.Errorf("unsupported os: %s", runtime.GOOS)
	}

	ffxPath, err := findToolPath(buildDir, "ffx")
	if err != nil {
		return nil, fmt.Errorf("unable to locate ffx: %s", err)
	}

	fxConfig := filepath.Join(buildDir, "fx.config")
	file, err := os.Open(fxConfig)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q: %s", fxConfig, err)
	}
	defer file.Close()

	properties := map[string]string{}
	re := regexp.MustCompile(`^([^=]+)=(?:'([^']+)'|(.+))?$`)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		m := re.FindStringSubmatch(scanner.Text())
		if m != nil {
			properties[m[1]] = m[2]
		}
	}

	arch, found := properties["FUCHSIA_ARCH"]
	if !found {
		return nil, fmt.Errorf("no arch in %s", fxConfig)
	}

	archInfo, ok := Archs[arch]
	if !ok {
		supported := make([]string, 0, len(Archs))
		for k := range Archs {
			supported = append(supported, k)
		}
		return nil, fmt.Errorf("unsupported arch: %s (supported: %v)", arch, supported)
	}

	binary := archInfo.Binary
	kernel := archInfo.Kernel
	platform += "-" + arch

	clangDir := filepath.Join(prebuiltDir, "third_party/clang", platform)
	qemuDir := filepath.Join(prebuiltDir, "third_party/qemu", platform)
	imgDir := filepath.Join(buildDir, "obj", "build", "images", "fuchsia", "fuchsia")

	build := &BaseBuild{
		Paths: map[string]string{
			"zbi":             filepath.Join(imgDir, "fuchsia.zbi"),
			"fvm":             filepath.Join(buildDir, hostDir, "fvm"),
			"zbitool":         filepath.Join(buildDir, hostDir, "zbi"),
			"blk":             filepath.Join(imgDir, "fvm.blk"),
			"qemu":            filepath.Join(qemuDir, "bin", binary),
			"kernel":          filepath.Join(buildDir, kernel),
			"llvm-symbolizer": filepath.Join(clangDir, "bin", "llvm-symbolizer"),
			"symbolizer":      filepath.Join(buildDir, hostDir, "symbolize"),
			"fuzzers.json":    filepath.Join(buildDir, "fuzzers.json"),
			"tests.json":      filepath.Join(buildDir, "tests.json"),
			"ffx":             ffxPath,
		},
		IDs: []string{
			filepath.Join(clangDir, "lib", "debug", ".build-id"),
			filepath.Join(buildDir, ".build-id"),
		},
	}
	if err := build.LoadFuzzers(); err != nil {
		return nil, err
	}

	return build, nil
}

// Subset of data format used in tool_paths.json
type toolMetadata struct {
	Name string
	Path string
}

// Resolve a tool by name using tool_paths.json in the buildDir. This is only
// used when working in a local Fuchsia checkout.
func findToolPath(buildDir string, toolName string) (string, error) {
	jsonPath := filepath.Join(buildDir, "tool_paths.json")
	jsonBlob, err := os.ReadFile(jsonPath)
	if err != nil {
		return "", fmt.Errorf("failed to read %q: %s", jsonPath, err)
	}
	var metadataList []toolMetadata
	if err := json.Unmarshal(jsonBlob, &metadataList); err != nil {
		return "", fmt.Errorf("failed to parse %q: %s", jsonPath, err)
	}

	for _, metadata := range metadataList {
		if metadata.Name == toolName {
			return filepath.Join(buildDir, metadata.Path), nil
		}
	}

	return "", fmt.Errorf("no path found for tool %q in %q", toolName, jsonPath)
}

// LoadFuzzers populates the build's map of Fuzzers. Unless an error is
// returned, any previously loaded fuzzers will be discarded.
func (b *BaseBuild) LoadFuzzers() error {
	v1Fuzzers, err := b.loadV1Fuzzers()
	if err != nil {
		return fmt.Errorf("error loading v1 fuzzers: %s", err)
	}

	v2Fuzzers, err := b.loadV2Fuzzers()
	if err != nil {
		return fmt.Errorf("error loading v2 fuzzers: %s", err)
	}

	b.Fuzzers = make(map[string]*Fuzzer)
	for name, fuzzer := range v1Fuzzers {
		b.Fuzzers[name] = fuzzer
	}
	for name, fuzzer := range v2Fuzzers {
		b.Fuzzers[name] = fuzzer
	}

	return nil
}

// Convenience type alias for heterogenous metadata objects in fuzzers.json
type fuzzerMetadata map[string]string

// loadV1Fuzzers reads and parses fuzzers.json.
func (b *BaseBuild) loadV1Fuzzers() (map[string]*Fuzzer, error) {
	paths, err := b.Path("fuzzers.json")
	if err != nil {
		return nil, err
	}

	jsonPath := paths[0]

	glog.Infof("Loading fuzzers from %q", jsonPath)

	jsonBlob, err := os.ReadFile(jsonPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %s", jsonPath, err)
	}

	var metadataList []fuzzerMetadata
	if err := json.Unmarshal(jsonBlob, &metadataList); err != nil {
		return nil, fmt.Errorf("failed to parse %q: %s", jsonPath, err)
	}

	// Condense metadata entries by label
	metadataByLabel := make(map[string]fuzzerMetadata)
	for _, metadata := range metadataList {
		label, found := metadata["label"]
		if !found {
			return nil, fmt.Errorf("failed to parse %q: entry missing label", jsonPath)
		}

		if _, found := metadataByLabel[label]; !found {
			metadataByLabel[label] = make(fuzzerMetadata)
		}

		for k, v := range metadata {
			if v != "" {
				metadataByLabel[label][k] = v
			}
		}
	}

	fuzzers := make(map[string]*Fuzzer)
	for label, metadata := range metadataByLabel {
		pkg, found := metadata["package"]
		if !found {
			return nil, fmt.Errorf("failed to parse %q: no package for %q", jsonPath, label)
		}

		fuzzer, found := metadata["fuzzer"]
		if !found {
			return nil, fmt.Errorf("failed to parse %q: no fuzzer for %q", jsonPath, label)
		}

		f := NewV1Fuzzer(b, pkg, fuzzer)
		fuzzers[f.Name] = f
	}

	return fuzzers, nil
}

// Relevant subset of tests.json metadata format
type testMetadata struct {
	BuildRule  string `json:"build_rule"`
	PackageUrl string `json:"package_url"`
}
type testEntry struct {
	Test testMetadata
}

// loadV2Fuzzers reads and parses tests.json.
func (b *BaseBuild) loadV2Fuzzers() (map[string]*Fuzzer, error) {
	ffxFuzzVal, ok := os.LookupEnv("UNDERCOAT_USE_FFX_FUZZ")
	if !ok {
		ffxFuzzVal = "0"
	}
	useFfxFuzz, err := strconv.ParseBool(ffxFuzzVal)
	if err != nil {
		return nil, fmt.Errorf("failed to parse UNDERCOAT_USE_FFX_FUZZ=%s: %s", ffxFuzzVal, err)
	}

	paths, err := b.Path("tests.json")
	if err != nil {
		return nil, err
	}

	jsonPath := paths[0]

	fuzzers := make(map[string]*Fuzzer)

	// During transition, only load v2 fuzzers opportunistically
	if !fileExists(jsonPath) {
		glog.Warningf("Skipping v2 fuzzers, because %q does not exist", jsonPath)
		return fuzzers, nil
	}

	glog.Infof("Loading fuzzers from %q", jsonPath)

	jsonBlob, err := os.ReadFile(jsonPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %s", jsonPath, err)
	}

	var testList []testEntry
	if err := json.Unmarshal(jsonBlob, &testList); err != nil {
		return nil, fmt.Errorf("failed to parse %q: %s", jsonPath, err)
	}

	pkgUrlRegex := regexp.MustCompile(`^fuchsia-pkg://[^/]+/([^#]+)#meta/([^\.]+)\.cm$`)
	for _, entry := range testList {
		if entry.Test.BuildRule != "fuchsia_fuzzer_package" {
			continue
		}
		m := pkgUrlRegex.FindStringSubmatch(entry.Test.PackageUrl)
		if m == nil {
			return nil, fmt.Errorf("found test with unexpected url: %v", entry)
		}
		fuzzer := NewV2Fuzzer(b, m[1], m[2], useFfxFuzz)
		fuzzers[fuzzer.Name] = fuzzer
	}

	return fuzzers, nil
}

// ListFuzzers lists the names of fuzzers present in the build, excluding any
// that we don't want ClusterFuzz to actually pick up. We can't just omit the
// example fuzzers from the build entirely because they are used in integration
// testing.
func (b *BaseBuild) ListFuzzers() []string {
	var names []string
	for _, fuzzer := range b.Fuzzers {
		if !fuzzer.IsExample() {
			names = append(names, fuzzer.Name)
		}
	}
	return names
}

// Fuzzer finds the Fuzzer with the given name, if available
func (b *BaseBuild) Fuzzer(name string) (*Fuzzer, error) {
	fuzzer, found := b.Fuzzers[name]
	if !found {
		return nil, fmt.Errorf("no such fuzzer: %s", name)
	}
	return fuzzer, nil
}

// Prepare is a no-op for simple builds
func (b *BaseBuild) Prepare() error {
	return nil
}

// Path returns the absolute paths to the list of files indicated by keys. This
// allows callers to abstract away the detail of where specific file resources
// are.
func (b *BaseBuild) Path(keys ...string) ([]string, error) {
	paths := make([]string, len(keys))
	for i, key := range keys {
		if path, found := b.Paths[key]; found {
			paths[i] = path
		} else {
			return nil, fmt.Errorf("no path for %q", key)
		}
	}
	return paths, nil
}

var logPrefixRegex = regexp.MustCompile(`[0-9\[\]\.]*\[klog\] (?:INFO|WARNING|ERROR): `)

// Remove timestamps, etc.
func stripLogPrefix(line string) string {
	return logPrefixRegex.ReplaceAllString(line, "")
}

// Symbolize reads from in and replaces symbolizer markup with debug
// information before writing the result to out.  This is blocking, and does
// not propagate EOFs from in to out.
func (b *BaseBuild) Symbolize(in io.ReadCloser, out io.Writer) error {
	// Close `in` on return so that the fuzzer doesn't block on a write if an
	// early exit occurs later in the output-processing chain.
	defer in.Close()

	paths, err := b.Path("symbolizer", "llvm-symbolizer")
	if err != nil {
		return err
	}

	symbolizer, llvmSymbolizer := paths[0], paths[1]

	args := []string{"-llvm-symbolizer", llvmSymbolizer}
	for _, dir := range b.IDs {
		args = append(args, "-build-id-dir", dir)
	}
	cmd := NewCommand(symbolizer, args...)
	cmd.Stdin = in
	pipe, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	if err := cmd.Start(); err != nil {
		return err
	}
	scanner := bufio.NewScanner(pipe)

	for scanner.Scan() {
		io.WriteString(out, stripLogPrefix(scanner.Text())+"\n")
	}

	if err := scanner.Err(); err != nil {
		return fmt.Errorf("failed during scan: %s", err)
	}

	return cmd.Wait()
}
