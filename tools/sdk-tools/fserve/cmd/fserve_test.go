// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"syscall"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

// test argument pointing to the directory containing the testdata directory.
// This is configured in the BUILD.gn file.
var testrootFlag = flag.String("testroot", "", "Root directory of the files needed to execute the test.")

const (
	resolvedAddr = "fe80::c0ff:eee:fe00:4444%en0"
	hostaddr     = "fe80::c0ff:eeee:fefe:c000%eth1"
	validDir     = "/some/package/repo/dir"
)

type testSDKProperties struct {
	dataPath              string
	expectCustomSSHConfig bool
	expectPrivateKey      bool
	expectSSHPort         bool
	expectedFFXArgs       [][]string
	expectedSSHArgs       [][]string
}

func (testSDK testSDKProperties) GetToolsDir() (string, error) {
	return "fake-tools", nil
}

func (testSDK testSDKProperties) GetSDKDataPath() string {
	return testSDK.dataPath
}

func (testSDK testSDKProperties) GetAvailableImages(version string, bucket string) ([]sdkcommon.GCSImage, error) {
	return []sdkcommon.GCSImage{}, nil
}

func (testSDK testSDKProperties) GetDefaultPackageRepoDir() (string, error) {
	return filepath.Join(testSDK.dataPath, "default-target-name", "packages", "amber-files"), nil
}

func (testSDK testSDKProperties) RunFFX(ffxArgs []string, interactive bool) (string, error) {
	expectedArgs := []string{}

	for _, args := range testSDK.expectedFFXArgs {
		if args[0] == args[0] {
			expectedArgs = args
			break
		}
	}

	ok := len(expectedArgs) == len(ffxArgs)
	if ok {
		for i, expected := range expectedArgs {
			if !ok {
				return "", fmt.Errorf("unexpected ffx args[%v]  %v expected[%v] %v",
					len(ffxArgs), ffxArgs, len(expectedArgs), expectedArgs)
			}
			if strings.Contains(expected, "*") {
				expectedPattern := regexp.MustCompile(expected)
				ok = expectedPattern.MatchString(ffxArgs[i])
			} else {
				ok = expected == ffxArgs[i]
			}
		}
		if !ok {
			return "", fmt.Errorf("unexpected  ffx args[%v]  %v expected[%v] %v",
				len(ffxArgs), ffxArgs, len(expectedArgs), expectedArgs)
		}
	}
	return "", nil
}

func (testSDK testSDKProperties) RunSSHCommand(targetAddress string, sshConfig string,
	privateKey string, sshPort string, verbose bool, sshArgs []string) (string, error) {

	if testSDK.expectCustomSSHConfig && sshConfig == "" {
		return "", errors.New("Expected custom ssh config file")
	}
	if testSDK.expectPrivateKey && privateKey == "" {
		return "", errors.New("Expected private key file")
	}

	if testSDK.expectSSHPort && sshPort == "" {
		return "", errors.New("Expected custom ssh port")
	}
	expectedArgs := []string{}

	for _, args := range testSDK.expectedSSHArgs {
		if sshArgs[0] == args[0] {
			expectedArgs = args
			break
		}
	}

	ok := len(expectedArgs) == len(sshArgs)
	if ok {
		for i, expected := range expectedArgs {
			if !ok {
				return "", fmt.Errorf("unexpected ssh args[%v]  %v expected[%v] %v",
					len(sshArgs), sshArgs, len(expectedArgs), expectedArgs)
			}
			if strings.Contains(expected, "*") {
				expectedPattern := regexp.MustCompile(expected)
				ok = expectedPattern.MatchString(sshArgs[i])
			} else {
				ok = expected == sshArgs[i]
			}
		}
		if !ok {
			return "", fmt.Errorf("unexpected ssh args[%v]  %v expected[%v] %v",
				len(sshArgs), sshArgs, len(expectedArgs), expectedArgs)
		}
	}
	if sshArgs[0] == "echo" {
		return fmt.Sprintf("%v 54545 fe80::c00f:f0f0:eeee:cccc 22\n", hostaddr), nil
	}
	return "", nil
}

// Context with a logger used to test.
func testingContext() context.Context {
	flags := log.Ltime | log.Lshortfile
	log := logger.NewLogger(logger.DebugLevel, color.NewColor(color.ColorAuto), os.Stdout, os.Stderr, "fserve_test ")
	log.SetFlags(flags)
	return logger.WithLogger(context.Background(), log)
}

// See exec_test.go for details, but effectively this runs the function called TestHelperProcess passing
// the args.
func helperCommandForFServe(command string, s ...string) (cmd *exec.Cmd) {
	cs := []string{"-test.run=TestFakeFServe", "--"}
	cs = append(cs, command)
	cs = append(cs, s...)

	cmd = exec.Command(os.Args[0], cs...)
	// Set this in the enviroment, so we can control the result.
	cmd.Env = append(os.Environ(), "GO_WANT_HELPER_PROCESS=1")
	return cmd
}

func clearEnvVars() {
	os.Unsetenv("FSERVE_TEST_NO_SERVERS")
	os.Unsetenv("FSERVE_TEST_NO_SERVERS_IS_ERROR")
	os.Unsetenv("FSERVE_TEST_PGREP_ERROR")
	os.Unsetenv("FSERVE_TEST_PS_EXIT_ERROR")
	os.Unsetenv("FSERVE_TEST_PS_ERROR")

	os.Unsetenv("TEST_LOGLEVEL")

	os.Unsetenv("_EXPECTED_ADD_SRC_ARGS")
	os.Unsetenv("_EXPECTED_RULE_REPLACE_ARGS")
	os.Unsetenv("_EXPECTED_FFX_REPOSITORY_ADD_ARGS")
	os.Unsetenv("_EXPECTED_FFX_REPOSITORY_REGISTER_ARGS")
	os.Unsetenv("_EXPECTED_FFX_REPOSITORY_SERVER_ARGS")
	os.Unsetenv("FSERVE_EXPECTED_ARGS")
	os.Unsetenv("_FAKE_FFX_DEVICE_CONFIG_DATA")
	os.Unsetenv("_FAKE_FFX_TARGET_DEFAULT")
	os.Unsetenv("_FAKE_FFX_TARGET_LIST")
	os.Unsetenv("_FAKE_FFX_GET_SSH_ADDRESS")
	os.Unsetenv("FFX_ISOLATE_DIR")
}

func clearTestEnv() {
	ExecCommand = exec.Command
	OsStat = os.Stat
	findProcess = defaultFindProcess
	syscallWait4 = defaultsyscallWait4

	sdkcommon.ExecCommand = exec.Command
	sdkcommon.GetUserHomeDir = sdkcommon.DefaultGetUserHomeDir
	sdkcommon.GetUsername = sdkcommon.DefaultGetUsername
	sdkcommon.GetHostname = sdkcommon.DefaultGetHostname
	sdkcommon.ExecLookPath = exec.LookPath

	clearEnvVars()
}

func TestCleanPmRepo(t *testing.T) {
	sdkcommon.ExecCommand = helperCommandForFServe
	ExecCommand = helperCommandForFServe
	ctx := testingContext()
	defer clearTestEnv()

	tests := []struct {
		path          string
		expectedError string
		doesPathExist bool
	}{
		{
			path:          validDir,
			doesPathExist: true,
		},
		{
			path:          "/some/dir/rm/error",
			expectedError: "exit status 1",
			doesPathExist: true,
		}, {
			path:          "/dir/does/not/exist",
			doesPathExist: false,
		},
	}

	for _, test := range tests {
		OsStat = func(name string) (fi os.FileInfo, err error) {
			var cmd os.FileInfo
			if test.doesPathExist {
				return cmd, nil
			}
			return nil, os.ErrNotExist
		}

		err := cleanPmRepo(ctx, test.path)
		if err != nil && err.Error() != test.expectedError {
			t.Errorf("cleanPmRepo() error doesn't match; got: '%v', want: '%v'", err, test.expectedError)
		}
	}
}

func TestKillPMServers(t *testing.T) {
	ctx := testingContext()
	defer clearTestEnv()

	tests := []struct {
		name               string
		noServers          string
		noServersIsError   string
		pgrepError         string
		psExitError        string
		psGenericError     string
		port               string
		expectedErrMessage string
	}{
		{
			name:             "No existing servers with no error",
			noServers:        "1",
			noServersIsError: "0",
		},
		{
			name:             "No existing servers with error",
			noServers:        "1",
			noServersIsError: "1",
		},
		{
			name:             "Existing servers with no error",
			noServers:        "0",
			noServersIsError: "0",
		},
		{
			name:             "Existing server on port 8083 with no error",
			noServers:        "0",
			noServersIsError: "0",
			port:             "8083",
		},
		{
			name:               "pgrep exits with error",
			pgrepError:         "1",
			expectedErrMessage: "Error running pgrep: Expected error\n",
		},
		{
			name:               "ps exits with exec.ExitError",
			pgrepError:         "0",
			psExitError:        "1",
			psGenericError:     "0",
			expectedErrMessage: "Error running ps expected exit error: exit status 1",
		},
		{
			name:               "ps exits with exec.Error",
			pgrepError:         "0",
			psExitError:        "0",
			psGenericError:     "1",
			expectedErrMessage: "Error running ps : exit status 1",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			clearTestEnv()
			ExecCommand = helperCommandForFServe
			findProcess = mockedFindProcess
			os.Setenv("FSERVE_TEST_NO_SERVERS", test.noServers)
			os.Setenv("FSERVE_TEST_NO_SERVERS_IS_ERROR", test.noServersIsError)
			os.Setenv("FSERVE_TEST_PGREP_ERROR", test.pgrepError)
			os.Setenv("FSERVE_TEST_PS_EXIT_ERROR", test.psExitError)
			os.Setenv("FSERVE_TEST_PS_ERROR", test.psGenericError)
			err := killPMServers(ctx, test.port)
			if err != nil {
				message := fmt.Sprintf("%v", err)
				if message != test.expectedErrMessage {
					t.Errorf("killPMServers() got error: '%s', want: '%s'", message, test.expectedErrMessage)
				}
			} else if test.expectedErrMessage != "" {
				t.Errorf("Got no error, want: '%s'", test.expectedErrMessage)
			}
		})
	}
}

func TestStartPMServer(t *testing.T) {
	testSDK := testSDKProperties{
		dataPath: "/fake",
	}
	repoPath := "/fake/repo/path"
	repoPort := "8083"
	defer clearTestEnv()

	tests := []struct {
		syscallWait4  func(pid int, wstatus *syscall.WaitStatus, flags int, usage *syscall.Rusage) (int, error)
		expectedError string
		logLevel      logger.LogLevel
		expectedArgs  []string
	}{
		{
			syscallWait4:  mockWait4NoError,
			expectedError: "",
			logLevel:      logger.WarningLevel,
			expectedArgs:  []string{"serve", "-q", "-repo", "/fake/repo/path", "-c", "2", "-l", ":8083"},
		},
		{
			syscallWait4:  mockWait4NoError,
			expectedError: "",
			logLevel:      logger.DebugLevel,
			expectedArgs:  []string{"serve", "-repo", "/fake/repo/path", "-c", "2", "-l", ":8083"},
		},
		{
			syscallWait4:  mockWait4WithError,
			expectedError: "Server started then exited with code 1",
			logLevel:      logger.WarningLevel,
		},
	}

	for i, test := range tests {
		t.Run(fmt.Sprintf("TestStartServer case %d", i), func(t *testing.T) {
			clearTestEnv()
			ExecCommand = helperCommandForFServe
			syscallWait4 = test.syscallWait4
			level = test.logLevel
			os.Setenv("TEST_LOGLEVEL", level.String())
			cmd, err := startPMServer(testSDK, repoPath, repoPort)
			if err != nil {
				actual := fmt.Sprintf("%v", err)
				if test.expectedError != actual {
					t.Errorf("Actual error [%v] did not match expected [%v]", actual, test.expectedError)
				}
			} else if test.expectedError != "" {
				t.Errorf("Expected error %v, but got no error", test.expectedError)
			} else {
				actual := cmd.Args[4:]
				ok := len(actual) == len(test.expectedArgs)
				if ok {
					for i, arg := range test.expectedArgs {
						if arg != actual[i] {
							ok = false
							break
						}
					}
				}
				if !ok {
					t.Errorf("pm args %v do not match expected %v", actual, test.expectedArgs)
				}
			}
		})
		syscallWait4 = defaultsyscallWait4
	}
}

func TestDownloadImageIfNeeded(t *testing.T) {
	testSDK := testSDKProperties{
		dataPath: t.TempDir(),
	}
	ctx := testingContext()
	ExecCommand = helperCommandForFServe
	sdkcommon.ExecCommand = helperCommandForFServe
	sdkcommon.ExecLookPath = func(cmd string) (string, error) { return filepath.Join("mocked", cmd), nil }
	defer clearTestEnv()

	version := "any-version"
	bucket := "test-bucket"
	srcPath := "gs://test-bucket/path/on/GCS/theImage.tgz"
	imageFilename := "theImage.tgz"
	repoPath, err := testSDK.GetDefaultPackageRepoDir()
	if err != nil {
		t.Fatal(err)
	}

	executable, _ := os.Executable()
	fmt.Fprintf(os.Stderr, "Running test executable %v\n", executable)
	fmt.Fprintf(os.Stderr, "testrootFlag value   is %v\n", *testrootFlag)
	testrootPath := filepath.Join(filepath.Dir(executable), *testrootFlag)
	os.Setenv("FSERVE_TEST_TESTROOT", testrootPath)

	if err := downloadImageIfNeeded(ctx, testSDK, version, bucket, srcPath, imageFilename, repoPath); err != nil {
		t.Fatal(err)
	}
	// Run the test again, and it should skip the download
	os.Setenv("FSERVE_TEST_ASSERT_NO_DOWNLOAD", "1")
	if err := downloadImageIfNeeded(ctx, testSDK, version, bucket, srcPath, imageFilename, repoPath); err != nil {
		t.Fatal(err)
	}
}

func TestDownloadImageIfNeededCopiedFails(t *testing.T) {
	testSDK := testSDKProperties{
		dataPath: "/fake",
	}
	ctx := testingContext()
	ExecCommand = helperCommandForFServe
	sdkcommon.ExecCommand = helperCommandForFServe
	sdkcommon.ExecLookPath = func(cmd string) (string, error) { return filepath.Join("mocked", cmd), nil }
	defer func() {
		ExecCommand = exec.Command
		sdkcommon.ExecCommand = exec.Command
		sdkcommon.ExecLookPath = exec.LookPath
		os.Setenv("FSERVE_TEST_COPY_FAILS", "")
	}()
	version := "any-version"
	bucket := "test-bucket"
	srcPath := "gs://test-bucket/path/on/GCS/theImage.tgz"
	imageFilename := "theImage.tgz"
	repoPath, err := testSDK.GetDefaultPackageRepoDir()
	if err != nil {
		t.Fatal(err)
	}

	// Run the test again, and it should skip the download
	os.Setenv("FSERVE_TEST_ASSERT_NO_DOWNLOAD", "")
	os.Setenv("FSERVE_TEST_COPY_FAILS", "1")
	if err := downloadImageIfNeeded(ctx, testSDK, version, bucket, srcPath, imageFilename, repoPath); err != nil {
		destPath := filepath.Join(testSDK.GetSDKDataPath(), imageFilename)
		expected := fmt.Sprintf("Could not copy image from %v to %v: BucketNotFoundException: 404 %v bucket does not exist.: exit status 2",
			srcPath, destPath, srcPath)
		actual := fmt.Sprintf("%v", err)
		if expected != actual {
			t.Fatalf("[%v], got [%v]", expected, actual)
		}
	} else {
		t.Fatal("Expected error downloading, got no error.")
	}
}

func TestRegisterPMRepository(t *testing.T) {
	testSDK := testSDKProperties{
		dataPath: t.TempDir(),
	}
	homeDir := filepath.Join(testSDK.GetSDKDataPath(), "_TEMP_HOME")
	if err := os.MkdirAll(homeDir, 0o700); err != nil {
		t.Fatal(err)
	}
	ctx := testingContext()
	ExecCommand = helperCommandForFServe
	sdkcommon.ExecCommand = helperCommandForFServe
	sdkcommon.GetUserHomeDir = func() (string, error) { return homeDir, nil }
	sdkcommon.GetUsername = func() (string, error) { return "testuser", nil }
	sdkcommon.GetHostname = func() (string, error) { return "testhost", nil }
	defer clearTestEnv()

	tests := []struct {
		repoPort        string
		targetAddress   string
		sshConfig       string
		name            string
		privateKey      string
		sshPort         string
		persist         bool
		expectedSSHArgs [][]string
	}{
		{
			repoPort:      "8083",
			targetAddress: resolvedAddr,
			sshConfig:     "",
			privateKey:    "",
			name:          "devhost",
			expectedSSHArgs: [][]string{
				{"echo", "$SSH_CONNECTION"},
				{"pkgctl", "repo", "add", "url", "-n", "devhost", "http://[fe80::c0ff:eeee:fefe:c000%25eth1]:8083/config.json"},
				{"pkgctl", "rule", "rule", "replace", "json", `'{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`},
			},
		},
		{
			repoPort:      "8083",
			targetAddress: resolvedAddr,
			sshConfig:     "custom-sshconfig",
			privateKey:    "",
			name:          "devhost",
			expectedSSHArgs: [][]string{
				{"echo", "$SSH_CONNECTION"},
				{"pkgctl", "repo", "add", "url", "-n", "devhost", "http://[fe80::c0ff:eeee:fefe:c000%25eth1]:8083/config.json"},
				{"pkgctl", "rule", "rule", "replace", "json", `'{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`},
			},
		},
		{
			repoPort:      "8083",
			targetAddress: resolvedAddr,
			sshConfig:     "",
			privateKey:    "private-key",
			name:          "devhost",
			expectedSSHArgs: [][]string{
				{"echo", "$SSH_CONNECTION"},
				{"pkgctl", "repo", "add", "url", "-n", "devhost", "http://[fe80::c0ff:eeee:fefe:c000%25eth1]:8083/config.json"},
				{"pkgctl", "rule", "rule", "replace", "json", `'{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`},
			},
		},
		{
			repoPort:      "8083",
			targetAddress: resolvedAddr,
			sshConfig:     "",
			privateKey:    "",
			name:          "devhost",
			sshPort:       "1022",
			expectedSSHArgs: [][]string{
				{"echo", "$SSH_CONNECTION"},
				{"pkgctl", "repo", "add", "url", "-n", "devhost", "http://[fe80::c0ff:eeee:fefe:c000%25eth1]:8083/config.json"},
				{"pkgctl", "rule", "rule", "replace", "json", `'{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`},
			},
		},
		{
			repoPort:      "8083",
			targetAddress: resolvedAddr,
			sshConfig:     "",
			privateKey:    "",
			name:          "devhost",
			sshPort:       "1022",
			persist:       true,
			expectedSSHArgs: [][]string{
				{"echo", "$SSH_CONNECTION"},
				{"pkgctl", "repo", "add", "url", "-p", "-n", "devhost", "http://[fe80::c0ff:eeee:fefe:c000%25eth1]:8083/config.json"},
				{"pkgctl", "rule", "rule", "replace", "json", `'{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`},
			},
		},
	}

	for _, test := range tests {
		testSDK := testSDKProperties{expectedSSHArgs: test.expectedSSHArgs,
			expectCustomSSHConfig: test.sshConfig != "",
			expectPrivateKey:      test.privateKey != "",
			expectSSHPort:         test.sshPort != ""}

		if err := registerPMRepository(ctx, testSDK, test.repoPort, test.name, test.targetAddress, test.sshConfig, test.privateKey, test.persist, test.sshPort); err != nil {
			t.Fatal(err)
		}
	}
}

func TestPrepareFromArchive(t *testing.T) {
	ctx := testingContext()
	executable, _ := os.Executable()
	testRoot := filepath.Join(filepath.Dir(executable), *testrootFlag)
	testdata := filepath.Join(testRoot, "testdata", "testdata.tgz")
	if !sdkcommon.FileExists(testdata) {
		testdata = filepath.Join("..", "testdata", "testdata.tgz")
	}
	archiveDir := t.TempDir()
	data := []byte("This is a test file")
	for i := 0; i < 3; i++ {
		if err := os.WriteFile(filepath.Join(archiveDir, fmt.Sprintf("file%d.txt", i)), data, 0o600); err != nil {
			t.Fatal(err)
		}
	}

	tests := []struct {
		archivePath string
		errString   string
		expected    []string
	}{
		{
			archivePath: "/path/does/not/exist",
			errString:   "Invalid archive path: /path/does/not/exist. Needs to be .tgz file or directory",
		},
		{
			archivePath: testdata,
			expected:    []string{"", "/data", "/data/one.json", "/greeting.txt", "/packages.md5"},
		},
		{
			archivePath: archiveDir,
			expected:    []string{"", "/file0.txt", "/file1.txt", "/file2.txt"},
		},
	}

	for idx, test := range tests {
		repoPath := filepath.Join(t.TempDir(), fmt.Sprintf("repo/path_%d", idx))
		err := prepareFromArchive(ctx, repoPath, test.archivePath)
		if err != nil {
			if test.errString != "" && test.errString != err.Error() {
				t.Fatalf("Unexpected error message: %v. Expected %v", err, test.errString)
			} else if test.errString == "" {
				t.Fatalf("Unexpected error: %v", err)
			}
		} else {
			actual, err := listDirectory(filepath.Dir(repoPath))
			if err != nil {
				t.Fatal(err)
			}
			expected := test.expected
			if len(actual) != len(expected) {
				t.Fatalf("Mismatch content size. Actual: %v %v. Expected: %v %v", len(actual), actual, len(expected), test.expected)
			}
			for i, value := range expected {
				if strings.TrimPrefix(actual[i], filepath.Dir(repoPath)) != value {
					t.Fatalf("Mismatch element %d  Actual: %v Expected: %v ", i, strings.TrimPrefix(actual[i], filepath.Dir(repoPath)), value)
				}
			}
		}
	}

}

func TestCopyDir(t *testing.T) {
	ctx := testingContext()

	// build a src dir with
	// empty directory
	// files, no directoryies
	// top level just directories
	// mixture
	emptyDir := t.TempDir()
	filesOnly := t.TempDir()
	dirsOnly := t.TempDir()
	dirsWithFiles := t.TempDir()
	mixedDir := t.TempDir()

	// build the files only src
	data := []byte("This is a test file")
	for i := 0; i < 3; i++ {
		if err := os.WriteFile(filepath.Join(filesOnly, fmt.Sprintf("file%d.txt", i)), data, 0o600); err != nil {
			t.Fatal(err)
		}
	}

	for i := 0; i < 3; i++ {
		dirName := fmt.Sprintf("dir_%d", i)
		err := os.Mkdir(filepath.Join(dirsOnly, dirName), 0o755)
		if err != nil {
			t.Fatal(err)
		}
	}

	for i := 0; i < 3; i++ {
		dirName := fmt.Sprintf("dir_%d", i)
		err := os.Mkdir(filepath.Join(dirsWithFiles, dirName), 0o755)
		if err != nil {
			t.Fatal(err)
		}
		for j := 0; j < 3; j++ {
			if err := os.WriteFile(filepath.Join(dirsWithFiles, dirName, fmt.Sprintf("file%d.txt", j)), data, 0o600); err != nil {
				t.Fatal(err)
			}
		}
	}

	for i := 0; i < 3; i++ {
		dirName := fmt.Sprintf("dir_%d", i)
		err := os.Mkdir(filepath.Join(mixedDir, dirName), 0o755)
		if err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(mixedDir, fmt.Sprintf("file%d.txt", i)), data, 0o600); err != nil {
			t.Fatal(err)
		}
		for j := 0; j < 3; j++ {
			if err := os.WriteFile(filepath.Join(mixedDir, dirName, fmt.Sprintf("file%d.txt", j)), data, 0o600); err != nil {
				t.Fatal(err)
			}
		}
	}

	tests := []struct {
		srcDir   string
		destDir  string
		expected int
	}{
		{
			srcDir:   emptyDir,
			destDir:  filepath.Join(t.TempDir(), "empty"),
			expected: 1,
		},
		{
			srcDir:   filesOnly,
			destDir:  filepath.Join(t.TempDir(), "filesOnly"),
			expected: 4,
		},
		{
			srcDir:   dirsOnly,
			destDir:  filepath.Join(t.TempDir(), "dirsOnly"),
			expected: 4,
		},
		{
			srcDir:   dirsWithFiles,
			destDir:  filepath.Join(t.TempDir(), "dirsWithFiles"),
			expected: 13,
		},
		{
			srcDir:   mixedDir,
			destDir:  filepath.Join(t.TempDir(), "mixedDir"),
			expected: 16,
		},
	}

	for _, test := range tests {
		err := copyDir(ctx, test.srcDir, test.destDir)
		if err != nil {
			t.Fatalf("Empty dir test failed: %v", err)
		}
		actual, err := listDirectory(test.destDir)
		if err != nil {
			t.Fatalf("Could not list dest dir for test %v: %v", test, err)
		}
		expected, err := listDirectory(test.srcDir)
		if err != nil {
			t.Fatalf("Could not list src dir for test %v: %v", test, err)
		}
		if len(expected) != test.expected {
			t.Fatalf("src dir content length wrong. Expected %d actual: %d", len(expected), test.expected)
		}
		if len(actual) != len(expected) {
			t.Fatalf("Mismatch content size. Actual: %v %v. Expected: %v %v", len(actual), actual, len(expected), test.expected)
		}
		for i, value := range expected {
			if strings.TrimPrefix(actual[i], test.destDir) != strings.TrimPrefix(value, test.srcDir) {
				t.Fatalf("Mismatch element %d  Actual: %v Expected: %v ", i, actual[i], value)
			}
		}

	}
}

func TestMain(t *testing.T) {
	dataDir := t.TempDir()
	savedArgs := os.Args
	savedCommandLine := flag.CommandLine
	ExecCommand = helperCommandForFServe
	sdkcommon.ExecCommand = helperCommandForFServe
	sdkcommon.ExecLookPath = func(name string) (string, error) {
		if name == "gsutil" {
			return "/path/to/fake/gsutil", nil
		}
		return exec.LookPath(name)
	}
	syscallWait4 = mockWait4NoError
	defer func() {
		clearTestEnv()
		os.Args = savedArgs
		flag.CommandLine = savedCommandLine
	}()

	if err := os.MkdirAll(dataDir+"/path/to/archive", 0755); err != nil {
		fmt.Fprintf(os.Stderr, "Error getting mkdir temp dir: %v\n", err)
		t.Fail()
	}
	tests := []struct {
		testName                             string
		args                                 []string
		deviceConfiguration                  string
		ffxTargetList                        string
		ffxTargetDefault                     string
		ffxConfigServerEnabled               bool
		ffxConfigServerListen                string
		ffxConfigServerMode                  string
		ffxTargetGetSSHAddress               string
		ffxRepositoryServerListen            string
		expectedPMArgs                       string
		expectedAddSrcArgs                   string
		expectedRuleReplaceArgs              string
		expectedFFXRepositoryServerStopArgs  string
		expectedFFXRepositoryServerStartArgs string
		expectedFFXRepositoryAddArgs         string
		expectedFFXRepositoryRegisterArgs    string
		expectedExitCode                     int
	}{
		{
			testName:                "server mode default, no configuration, with 1 device discoverable",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--image", "test-image", "--version", "1.0.0", "--level", "debug"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                "server mode default, no configuration, fetching from a custom bucket",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--bucket", "test-bucket", "--image", "test-image", "--version", "1.0.0"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                "server mode default, stopping after clean",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--clean", "--image", "test-image", "--version", "1.0.0"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:               "server mode default, stopping after printing the default package repo path",
			args:                   []string{os.Args[0], "-data-path", dataDir, "-get-package-repo-path", "--image", "test-image", "--version", "1.0.0"},
			ffxTargetList:          `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:22`,
		},
		{
			testName:                "server mode default, custom device ip address and repo dir",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--device-ip", "::2", "--image", "test-image", "--version", "1.0.0", "--repo-dir", dataDir + "/custom/packages/amber-files"},
			expectedPMArgs:          "serve -repo " + dataDir + "/custom/packages/amber-files -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::2 pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::2 pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:               "server mode default, stopping after kill",
			args:                   []string{os.Args[0], "-data-path", dataDir, "--kill", "--version", "1.0.0"},
			ffxTargetList:          `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:22`,
		},
		{
			testName:                "server mode default, custom repo name",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--name", "test-devhost", "--image", "test-image", "--version", "1.0.0"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n test-devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"test-devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                "server mode default, custom package archive",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                "server mode default, persisted metadata",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive", "--persist"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -p -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:               "server mode default, stopping after prepare",
			args:                   []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive", "--prepare"},
			ffxTargetList:          `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:22`,
		},
		{
			testName:                "server mode default, custom private key",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive", "--private-key", "/path/to/key"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -i /path/to/key -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -i /path/to/key -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                "server mode default, custom sshconfig",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive", "--sshconfig", "/path/to/custom/sshconfig"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F /path/to/custom/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json"),
			expectedRuleReplaceArgs: `-F /path/to/custom/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`,
		},
		{
			testName:                "server mode default, custom server port",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive", "--server-port", "8999"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8999",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8999/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                "server mode default, custom device name",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--device-name", "test-device", "--image", "test-image", "--version", "1.0.0"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "test-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"test-device","rcs_state":"N","serial":"N","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:       "server mode default, with device configuration",
			args:           []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive"},
			expectedPMArgs: "serve -repo " + filepath.Join(dataDir, "remote-target-name/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:  `[{"nodename":"remote-target-name","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			deviceConfiguration: `
			{
				"remote-target-name": {
					"bucket":"fuchsia-bucket",
					"device-name":"remote-target-name",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "true"
				},
				"test-device":{
					"bucket":"fuchsia-bucket",
					"device-name":"test-device",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "false"
				}
			}`,
			ffxTargetGetSSHAddress:  `[::1f]:2202`,
			ffxTargetDefault:        "remote-target-name",
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig  -p 2202 -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -p 2202 -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:       "server mode default, with device configuration, selecting non-default device",
			args:           []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive", "--device-name", "test-device"},
			expectedPMArgs: "serve -repo " + filepath.Join(dataDir, "test-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList: `[{"nodename":"remote-target-name","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]},
			{"nodename":"test-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::ff"]}]`,
			ffxTargetGetSSHAddress: `[::ff]:22`,
			deviceConfiguration: `
			{
				"remote-target-name":{
					"bucket":"fuchsia-bucket",
					"device-name":"remote-target-name",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "true"
				},
				"test-device":{
					"bucket":"fuchsia-bucket",
					"device-name":"test-device",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "false"
				}
			}`,
			ffxTargetDefault:        "remote-target-name",
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::ff pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::ff pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                "server mode pm works from ffx config",
			args:                    []string{os.Args[0], "-data-path", dataDir, "--image", "test-image", "--version", "1.0.0", "--level", "debug"},
			expectedPMArgs:          "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			ffxTargetList:           `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:  `[::1f]:22`,
			ffxConfigServerMode:     "\"pm\"",
			expectedAddSrcArgs:      fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs: fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                            "server mode pm shuts down ffx repository server",
			args:                                []string{os.Args[0], "-data-path", dataDir, "--image", "test-image", "--version", "1.0.0", "--level", "debug"},
			ffxTargetList:                       `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:              `[::1f]:22`,
			ffxConfigServerEnabled:              true,
			ffxConfigServerListen:               "\"[::]:8083\"",
			ffxConfigServerMode:                 "\"pm\"",
			expectedPMArgs:                      "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			expectedFFXRepositoryServerStopArgs: "--config ffx_repository=true repository server stop",
			expectedAddSrcArgs:                  fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs:             fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                            "server mode pm does not shut down ffx repository server if on different port",
			args:                                []string{os.Args[0], "-data-path", dataDir, "--image", "test-image", "--version", "1.0.0", "--level", "debug"},
			ffxTargetList:                       `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:              `[::1f]:22`,
			ffxConfigServerEnabled:              true,
			ffxConfigServerListen:               "\"[::]:8084\"",
			ffxConfigServerMode:                 "\"pm\"",
			expectedPMArgs:                      "serve -repo " + filepath.Join(dataDir, "some-device/packages/amber-files") + " -c 2 -l :8083",
			expectedFFXRepositoryServerStopArgs: "--config ffx_repository=true repository server stop",
			expectedAddSrcArgs:                  fmt.Sprintf("-F %s/sshconfig -v ::1f pkgctl repo add url -n devhost http://[fe80::c0ff:eeee:fefe:c000%%25eth1]:8083/config.json", dataDir),
			expectedRuleReplaceArgs:             fmt.Sprintf(`-F %s/sshconfig -v ::1f pkgctl rule replace json '{"version":"1","content":[{"host_match":"fuchsia.com","host_replacement":"devhost","path_prefix_match":"/","path_prefix_replacement":"/"}]}'`, dataDir),
		},
		{
			testName:                             "server mode ffx works from ffx config",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--image", "test-image", "--version", "1.0.0", "--level", "debug"},
			ffxTargetList:                        `[{"nodename":"test-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "test-device/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target ::1f target repository register --repository devhost --alias fuchsia.com",
		},
		{
			testName:                             "server mode ffx supports custom repository names",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--name", "test-devhost", "--image", "test-image", "--version", "1.0.0"},
			ffxTargetList:                        `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository test-devhost " + filepath.Join(dataDir, "some-device/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target ::1f target repository register --repository test-devhost --alias fuchsia.com",
		},
		{
			testName:                             "server mode ffx supports custom device ip addresses and repository paths",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--device-ip", "::2", "--image", "test-image", "--version", "1.0.0", "--repo-dir", dataDir + "/custom/packages/amber-files"},
			ffxTargetList:                        `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "custom/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target ::2 target repository register --repository devhost --alias fuchsia.com",
		},
		{
			testName:      "server mode ffx supports using device config",
			args:          []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive"},
			ffxTargetList: `[{"nodename":"remote-target-name","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			deviceConfiguration: `
			{
				"remote-target-name":{
					"bucket":"fuchsia-bucket",
					"device-name":"remote-target-name",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "true"
				},
				"test-device":{
					"bucket":"fuchsia-bucket",
					"device-name":"test-device",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "false"
				}
			}`,
			ffxTargetGetSSHAddress:               `[::1f]:2202`,
			ffxTargetDefault:                     "remote-target-name",
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "remote-target-name/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target [::1f]:2202 target repository register --repository devhost --alias fuchsia.com",
		},
		{
			testName:               "server mode ffx, stopping after printing the default package repo path",
			args:                   []string{os.Args[0], "-data-path", dataDir, "-get-package-repo-path", "--image", "test-image", "--version", "1.0.0"},
			ffxTargetList:          `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:22`,
			ffxConfigServerMode:    "\"ffx\"",
		},
		{
			testName:                             "server mode ffx, stopping after kill",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--kill", "--version", "1.0.0"},
			ffxTargetList:                        `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
		},
		{
			testName:                             "server mode ffx does not support `-server-port`",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--server-port", "8899", "--version", "1.0.0"},
			ffxTargetList:                        `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "some-device/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target ::1f target repository register --repository devhost --alias fuchsia.com",
			expectedExitCode:                     1,
		},
		{
			testName:                             "server mode ffx ignores `-private-key`",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--private-key", "/path/to/custom/private-key", "--package-archive", dataDir + "/path/to/archive"},
			ffxTargetList:                        `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "some-device/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target ::1f target repository register --repository devhost --alias fuchsia.com",
		},
		{
			testName:                             "server mode ffx ignores `--sshconfig`",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--sshconfig", "/path/to/custom/sshconfig", "--package-archive", dataDir + "/path/to/archive"},
			ffxTargetList:                        `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "some-device/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target ::1f target repository register --repository devhost --alias fuchsia.com",
		},
		{
			testName:                             "server mode ffx supports persisting metadata",
			args:                                 []string{os.Args[0], "-data-path", dataDir, "--package-archive", dataDir + "/path/to/archive", "--persist"},
			ffxTargetList:                        `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress:               `[::1f]:22`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "some-device/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target ::1f target repository register --repository devhost --alias fuchsia.com --storage-type persistent",
		},
		{
			testName:               "server mode ffx supports ipv4 and custom ssh port",
			args:                   []string{os.Args[0], "-data-path", dataDir, "--image", "test-image", "--version", "1.0.0", "--repo-dir", dataDir + "/custom/packages/amber-files"},
			ffxTargetList:          `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["127.0.0.1"]}]`,
			ffxTargetGetSSHAddress: `127.0.0.1:10002`,
			deviceConfiguration: `{
				"remote-target-name": {
					"bucket":"fuchsia-bucket",
					"device-name":"remote-target-name",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "true",
					"device-port": "10002"
				}
			}`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "custom/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target 127.0.0.1:10002 target repository register --repository devhost --alias fuchsia.com",
		},
		{
			testName:               "server mode ffx supports ipv6 and custom ssh port",
			args:                   []string{os.Args[0], "-data-path", dataDir, "--image", "test-image", "--version", "1.0.0", "--repo-dir", dataDir + "/custom/packages/amber-files"},
			ffxTargetList:          `[{"nodename":"some-device","rcs_state":"N","serial":"<unknown>","target_type":"Unknown","target_state":"Product","addresses":["::1f"]}]`,
			ffxTargetGetSSHAddress: `[::1f]:10002`,
			deviceConfiguration: `{
				"remote-target-name": {
					"bucket":"fuchsia-bucket",
					"device-name":"remote-target-name",
					"image":"release",
					"package-port":"",
					"package-repo":"",
					"default": "true",
					"device-port": "10002"
				}
			}`,
			ffxConfigServerMode:                  "\"ffx\"",
			expectedFFXRepositoryServerStopArgs:  "--config ffx_repository=true repository server stop",
			expectedFFXRepositoryServerStartArgs: "--config ffx_repository=true repository server start",
			expectedFFXRepositoryAddArgs:         "--config ffx_repository=true repository add-from-pm --repository devhost " + filepath.Join(dataDir, "custom/packages/amber-files"),
			expectedFFXRepositoryRegisterArgs:    "--config ffx_repository=true --target [::1f]:10002 target repository register --repository devhost --alias fuchsia.com",
		},
	}

	for _, test := range tests {
		t.Run(test.testName, func(t *testing.T) {
			clearEnvVars()
			os.Args = test.args
			os.Setenv("FSERVE_TEST_NO_SERVERS", "1")
			os.Setenv("_EXPECTED_ADD_SRC_ARGS", test.expectedAddSrcArgs)
			os.Setenv("_EXPECTED_RULE_REPLACE_ARGS", test.expectedRuleReplaceArgs)
			os.Setenv("_EXPECTED_FFX_REPOSITORY_ADD_ARGS", test.expectedFFXRepositoryAddArgs)
			os.Setenv("_EXPECTED_FFX_REPOSITORY_REGISTER_ARGS", test.expectedFFXRepositoryRegisterArgs)
			os.Setenv("_EXPECTED_FFX_REPOSITORY_SERVER_START_ARGS", test.expectedFFXRepositoryServerStartArgs)
			os.Setenv("_EXPECTED_FFX_REPOSITORY_SERVER_STOP_ARGS", test.expectedFFXRepositoryServerStopArgs)
			os.Setenv("FSERVE_EXPECTED_ARGS", test.expectedPMArgs)
			os.Setenv("_FAKE_FFX_DEVICE_CONFIG_DATA", test.deviceConfiguration)
			os.Setenv("_FAKE_FFX_TARGET_DEFAULT", test.ffxTargetDefault)
			os.Setenv("_FAKE_FFX_TARGET_LIST", test.ffxTargetList)
			os.Setenv("FFX_ISOLATE_DIR", t.TempDir())
			if test.ffxConfigServerEnabled {
				os.Setenv("_FAKE_FFX_CONFIG_GET_SERVER_ENABLED", "true")
			} else {
				os.Setenv("_FAKE_FFX_CONFIG_GET_SERVER_ENABLED", "false")
			}
			os.Setenv("_FAKE_FFX_CONFIG_GET_SERVER_LISTEN", test.ffxConfigServerListen)
			os.Setenv("_FAKE_FFX_CONFIG_GET_SERVER_MODE", test.ffxConfigServerMode)
			os.Setenv("_FAKE_FFX_GET_SSH_ADDRESS", test.ffxTargetGetSSHAddress)
			flag.CommandLine = flag.NewFlagSet(os.Args[0], flag.ExitOnError)
			osExit = func(code int) {
				if code != test.expectedExitCode {
					t.Fatalf("Expected exit code [%v], got [%v]", test.expectedExitCode, code)
				}
			}
			main()
		})
	}
}

func listDirectory(dirname string) ([]string, error) {
	contents := []string{}
	err := filepath.Walk(dirname,
		func(path string, _ os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			contents = append(contents, path)
			return nil
		})
	return contents, err
}

/*
This "test" is used to mock the command line tools invoked by fserve.
The method "helperCommandForFServe" replaces exec.Command and runs
this test inplace of the command.

This approach to mocking out executables is  based on  exec_test.go.
*/
func TestFakeFServe(t *testing.T) {
	t.Helper()
	if os.Getenv("GO_WANT_HELPER_PROCESS") != "1" {
		return
	}
	defer os.Exit(0)

	args := os.Args
	for len(args) > 0 {
		if args[0] == "--" {
			args = args[1:]
			break
		}
		args = args[1:]
	}
	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "No command\n")
		os.Exit(2)
	}
	// Check the command line
	cmd, args := args[0], args[1:]
	switch filepath.Base(cmd) {
	case "pgrep":
		fakePgrep(args)
	case "ps":
		fakePS(args)
	case "rm":
		fakeRmRf(args)
	case "pm":
		fakePM(args)
	case "gsutil":
		fakeGSUtil(args)
	case "ffx":
		fakeFFX(args)
	case "ssh":
		fakeSSH(args)
	case "ssh-keygen":
		fakeKeygen(args)
	default:
		fmt.Fprintf(os.Stderr, "Unexpected command %v", cmd)
		os.Exit(1)
	}
}

func fakeKeygen(args []string) {
	// accept all invocations
	os.Exit(0)
}

func checkLen(expected, actual []string) {
	if len(actual) != len(expected) {
		fmt.Fprintf(os.Stderr, "Argument count mismatch. Expected %v, actual: %v\n", len(expected), len(actual))
		fmt.Fprintf(os.Stderr, "Expected: %v\n", expected)
		fmt.Fprintf(os.Stderr, "Actual  : %v\n", actual)
		os.Exit(1)
	}
}

func checkFields(expected, actual []string) {
	for i := range actual {
		if actual[i] != expected[i] {
			fmt.Fprintf(os.Stderr,
				"Mismatched args index %v. Expected: %v actual: %v\n",
				i, expected[i], actual[i])
			fmt.Fprintf(os.Stderr, "Full args Expected: %v actual: %v",
				expected, actual)
			os.Exit(3)
		}
	}
}

func fakeSSH(args []string) {
	if args[len(args)-1] == "$SSH_CONNECTION" {
		fmt.Printf("%v 54545 fe80::c00f:f0f0:eeee:cccc 22\n", hostaddr)
		os.Exit(0)
	}

	if strings.HasSuffix(args[len(args)-1], "config.json") || strings.HasSuffix(args[len(args)-2], "config.json") {
		expected := strings.Fields(os.Getenv("_EXPECTED_ADD_SRC_ARGS"))
		checkLen(expected, args)
		checkFields(expected, args)
		os.Exit(0)
	}

	if len(args) > 4 && strings.Contains(args[len(args)-4], "rule") {
		expected := strings.Fields(os.Getenv("_EXPECTED_RULE_REPLACE_ARGS"))
		checkLen(expected, args)
		checkFields(expected, args)
		os.Exit(0)
	}

	fmt.Fprintf(os.Stderr, "Unknown mocked ssh command: %v", args)
	os.Exit(2)

}

func fakeFFX(args []string) {
	if args[0] == "config" && args[1] == "get" {
		if args[2] == "DeviceConfiguration" || args[2] == "device_config" {
			fmt.Printf(os.Getenv("_FAKE_FFX_DEVICE_CONFIG_DATA"))
			os.Exit(0)
		} else if args[2] == "DeviceConfiguration.remote-target-name" || args[2] == "device_config.remote-target-name" {
			fmt.Println(`{"bucket":"","device-ip":"","device-name":"remote-target-name","image":"","package-port":"","package-repo":"/some/custom/repo/path","ssh-port":""}`)
			os.Exit(0)
		} else if args[2] == ffxConfigServerEnabledKey {
			if v, ok := os.LookupEnv("_FAKE_FFX_CONFIG_GET_SERVER_ENABLED"); ok {
				fmt.Printf(v)
				os.Exit(0)
			} else {
				os.Exit(2)
			}
		} else if args[2] == ffxConfigServerListenKey {
			if v, ok := os.LookupEnv("_FAKE_FFX_CONFIG_GET_SERVER_LISTEN"); ok {
				fmt.Printf(v)
				os.Exit(0)
			} else {
				os.Exit(2)
			}
		} else if args[2] == ffxConfigServerModeKey {
			if v, ok := os.LookupEnv("_FAKE_FFX_CONFIG_GET_SERVER_MODE"); ok {
				fmt.Printf(v)
				os.Exit(0)
			} else {
				os.Exit(2)
			}
		}

	}
	if args[0] == "config" && (args[1] == "set" || args[1] == "remove") {
		os.Exit(0)
	}

	if args[0] == "target" && args[1] == "default" && args[2] == "get" {
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_TARGET_DEFAULT"))
		os.Exit(0)
	}

	if args[0] == "--machine" && args[1] == "json" && args[2] == "target" && args[3] == "list" {
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_TARGET_LIST"))
		os.Exit(0)
	}

	if args[2] == "target" && args[3] == "get-ssh-address" {
		fmt.Printf("%v\n", os.Getenv("_FAKE_FFX_GET_SSH_ADDRESS"))
		os.Exit(0)
	}

	if args[0] == "--config" && args[1] == "ffx_repository=true" && args[2] == "--target" && args[4] == "target" && args[5] == "repository" && args[6] == "register" {
		expected := strings.Fields(os.Getenv("_EXPECTED_FFX_REPOSITORY_REGISTER_ARGS"))
		checkLen(expected, args)
		checkFields(expected, args)
		os.Exit(0)
	}

	if args[0] == "--config" && args[1] == "ffx_repository=true" && args[2] == "repository" && args[3] == "add-from-pm" {
		expected := strings.Fields(os.Getenv("_EXPECTED_FFX_REPOSITORY_ADD_ARGS"))
		checkLen(expected, args)
		checkFields(expected, args)
		os.Exit(0)
	}

	if args[0] == "--config" && args[1] == "ffx_repository=true" && args[2] == "repository" && args[3] == "server" && args[4] == "start" {
		expected := strings.Fields(os.Getenv("_EXPECTED_FFX_REPOSITORY_SERVER_START_ARGS"))
		checkLen(expected, args)
		checkFields(expected, args)
		os.Exit(0)
	}

	if args[0] == "--config" && args[1] == "ffx_repository=true" && args[2] == "repository" && args[3] == "server" && args[4] == "stop" {
		expected := strings.Fields(os.Getenv("_EXPECTED_FFX_REPOSITORY_SERVER_STOP_ARGS"))
		checkLen(expected, args)
		checkFields(expected, args)
		os.Exit(0)
	}

	fmt.Fprintf(os.Stderr, "Unexpected ffx sub command: %v", args)
	os.Exit(2)
}

func fakeGSUtil(args []string) {
	expected := []string{}
	expectedLS := [][]string{
		{"ls", "gs://test-bucket/path/on/GCS/theImage.tgz"},
		{"ls", "gs://fuchsia/development/1.0.0/packages/test-image.tar.gz"},
	}
	expectedCP := [][]string{
		{"cp", "gs://test-bucket/path/on/GCS/theImage.tgz", "/.*/theImage.tgz"},
		{"cp", "gs://fuchsia/development/1.0.0/packages/test-image.tar.gz", "/.*/1.0.0_test-image.tar.gz"},
	}

	if len(args) == 0 {
		fmt.Fprintf(os.Stderr, "Expected arguments to gsutil\n")
		os.Exit(1)
	}
	switch args[0] {
	case "ls":
		expected = expectedLS[0]
		for _, ls := range expectedLS {
			if args[1] == ls[1] {
				expected = ls
			}
		}
	case "cp":
		expected = expectedCP[0]
		for _, cp := range expectedCP {
			if args[1] == cp[1] {
				expected = cp
			}
		}
		if os.Getenv("FSERVE_TEST_ASSERT_NO_DOWNLOAD") != "" {
			fmt.Fprintf(os.Stderr, "Unexpected call to gsutil cp: %v\n", args)
			os.Exit(1)
		}
		if os.Getenv("FSERVE_TEST_COPY_FAILS") != "" {
			fmt.Fprintf(os.Stderr, "BucketNotFoundException: 404 %v bucket does not exist.", args[1])
			os.Exit(2)
		}
		// Copy the test data to the expected path.
		testRoot := os.Getenv("FSERVE_TEST_TESTROOT")
		testdata := filepath.Join(testRoot, "testdata", "testdata.tgz")
		if !sdkcommon.FileExists(testdata) {
			testdata = filepath.Join("..", "testdata", "testdata.tgz")
		}
		if err := os.MkdirAll(filepath.Dir(args[2]), 0755); err != nil {
			fmt.Fprintf(os.Stderr, "Error getting mkdir temp dir: %v\n", err)
			os.Exit(1)
		}
		if err := copyFile(testdata, args[2]); err != nil {
			fmt.Fprintf(os.Stderr, "Error linking testdata: %v\n", err)
			os.Exit(1)
		}

	}

	ok := len(args) == len(expected)
	if ok {
		for i := range args {
			if strings.Contains(expected[i], "*") {
				expectedPattern := regexp.MustCompile(expected[i])
				ok = ok && expectedPattern.MatchString(args[i])
			} else {
				ok = ok && args[i] == expected[i]
			}
		}
	}
	if !ok {
		fmt.Fprintf(os.Stderr, "unexpected gsutil args  %v. Expected %v", args, expected)
		os.Exit(1)
	}
}

func fakePM(args []string) {
	expected := []string{"serve"}

	if os.Getenv("FSERVE_EXPECTED_ARGS") != "" {
		expected = strings.Fields(os.Getenv("FSERVE_EXPECTED_ARGS"))
	} else {
		logLevel := os.Getenv("TEST_LOGLEVEL")
		// only debug and trace have non-quiet mode.
		if logLevel != "debug" && logLevel != "trace" {
			expected = append(expected, "-q")
		}
		expected = append(expected, "-repo", "/fake/repo/path", "-c", "2", "-l", ":8083")
	}
	ok := len(args) == len(expected)
	if ok {
		for i := range args {
			ok = ok && args[i] == expected[i]
		}
	}
	if !ok {
		fmt.Fprintf(os.Stderr, "unexpected pm args  %v. Expected %v\n", args, expected)
		os.Exit(1)
	}
}

func fakePgrep(args []string) {
	if os.Getenv("FSERVE_TEST_PGREP_ERROR") == "1" {
		fmt.Fprintf(os.Stderr, "Expected error\n")
		os.Exit(1)
	}
	if args[0] == "pm" {
		if os.Getenv("FSERVE_TEST_NO_SERVERS") == "1" {
			// mac exits with 1
			if runtime.GOOS == "darwin" || os.Getenv("FSERVE_TEST_NO_SERVERS_IS_ERROR") == "1" {
				os.Exit(1)
			}
			os.Exit(0)
		} else {
			// return 3 pm instances
			fmt.Printf(`1000
		2000
		3000`)
			os.Exit(0)
		}
	}
	fmt.Fprintf(os.Stderr, "unexpected pgrep args  %v", args)
	os.Exit(1)
}

func fakePS(args []string) {
	if os.Getenv("FSERVE_TEST_PS_EXIT_ERROR") == "1" {
		fmt.Fprintf(os.Stderr, "expected exit error")
		os.Exit(1)
	}
	if os.Getenv("FSERVE_TEST_PS_ERROR") == "1" {
		fmt.Errorf("some ps error")
		os.Exit(1)
	}
	fmt.Println("    PID TTY      STAT   TIME COMMAND")
	for _, arg := range args {
		switch arg {
		case "1000":
			// some internal process
			fmt.Println("1000 ?       I<     0:00  [tpm_dev_wq]")
		case "2000":
			// pm on port 8083
			fmt.Println("2000  pts/0    Sl     0:00 /sdk/path/tools/x64/pm serve -repo /home/developer/.fuchsia/packages/amber-files -l :8083")
		case "3000":
			// pm on port 8084
			fmt.Println("3000  pts/0    Sl     0:00 /sdk/path/tools/x64/pm serve -repo /home/developer/.fuchsia/packages/amber-files -l :8084 -q")
		}
	}
}

func fakeRmRf(args []string) {
	command := strings.Join(args, " ")
	if command == fmt.Sprintf("-Rf %v", validDir) {
		os.Exit(0)
	}
	fmt.Fprintf(os.Stderr, "Directory does not exist\n")
	os.Exit(1)
}

type testProcess struct {
	Pid int
}

func (proc testProcess) Kill() error {
	// This mock only kills pid == 2000
	if proc.Pid != 2000 {
		return fmt.Errorf("Unexpected pid %v in Kill", proc.Pid)
	}
	return nil
}

func mockedFindProcess(pid int) (osProcess, error) {
	proc := testProcess{Pid: pid}

	return &proc, nil
}

func mockWait4NoError(pid int, wstatus *syscall.WaitStatus, flags int, usage *syscall.Rusage) (int, error) {
	return 0, nil
}

func mockWait4WithError(pid int, wstatus *syscall.WaitStatus, flags int, usage *syscall.Rusage) (int, error) {
	// set wstatus to exited with code 1.
	*wstatus = syscall.WaitStatus(0x100)
	return 0, nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	if err != nil {
		return err
	}
	return out.Close()
}
