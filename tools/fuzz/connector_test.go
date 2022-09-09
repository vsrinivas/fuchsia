// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/golang/glog"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/kr/fs"
)

// These SSHConnector tests connect to an in-memory SSH server (see ssh_fake.go
// for details), so we have good coverage of the SSH/SFTP mechanics. However,
// on the remote side, they rely on mocked commands and a mocked filesystem so
// do not test interaction with an actual instance.  For that, we rely on the
// end-to-end tests in e2e_test.go.

func TestSSHConnectorHandle(t *testing.T) {
	c := NewSSHConnector(nil, "somehost", 123, "keyfile", "/tmp/path")

	handle, err := NewHandleWithData(HandleData{connector: c})
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}
	defer handle.Release()

	build, _ := newMockBuild()

	// Note: we don't serialize here because that is covered by handle tests
	reloadedConn, err := loadConnectorFromHandle(build, handle)
	if err != nil {
		t.Fatalf("error loading connector from handle: %s", err)
	}

	c2, ok := reloadedConn.(*SSHConnector)
	if !ok {
		t.Fatalf("incorrect connector type")
	}

	if diff := cmp.Diff(c, c2, cmpopts.IgnoreUnexported(SSHConnector{})); diff != "" {
		t.Fatalf("incorrect data in reloaded connector (-want +got):\n%s", diff)
	}
}

func TestIncompleteSSHConnectorHandle(t *testing.T) {
	// Construct an object that isn't fully initialized
	c := NewSSHConnector(nil, "", 123, "", "")

	handle, err := NewHandleWithData(HandleData{connector: c})
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}
	defer handle.Release()

	build, _ := newMockBuild()
	if _, err := loadConnectorFromHandle(build, handle); err == nil {
		t.Fatalf("expected error, but succeeded")
	}
}

func TestSSHConnectorTmpDir(t *testing.T) {
	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	if err := c.Connect(); err != nil {
		t.Fatalf("error calling Connect: %s", err)
	}

	if c.TmpDir == "" {
		t.Fatalf("TmpDir was not set on Connect")
	}

	originalTmpDir := c.TmpDir

	c.Close()

	// The testing SSH server implementation doesn't support reconnection, so
	// we cheat by spinning up a second one and copying over the connection
	// info to the first connector.
	c2, _ := getFakeSSHConnector(t, 0)
	defer c2.Cleanup()

	c.Host = c2.Host
	c.Port = c2.Port
	c.Key = c2.Key

	if err := c.Connect(); err != nil {
		t.Fatalf("error reconnecting: %s", err)
	}

	if c.TmpDir != originalTmpDir {
		t.Fatalf("TmpDir changed after reconnection: %s", c.TmpDir)
	}

	c.Cleanup()

	expectPathAbsent(t, c.TmpDir)
}

func TestSSHCommand(t *testing.T) {
	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	arg := "some cool args"
	cmd := c.Command("echo", arg)
	out, err := cmd.Output()
	if err != nil {
		t.Fatalf("error running remote command: %s", err)
	}

	if string(out) != arg+"\n" {
		t.Fatalf("unexpected output: %q", string(out))
	}
}

func TestFfxRun(t *testing.T) {
	// Enable subprocess mocking (for ffx)
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	out, err := c.FfxRun("", "fuzz", "list")
	if err != nil {
		t.Fatalf("error running ffx command: %s", err)
	}

	if !strings.Contains(out, "fuchsia-pkg://fuchsia.com/ffx-fuzzers#meta/one.cm") {
		t.Fatalf("unexpected output: %q", out)
	}
}

func TestFfxRunPathRewriting(t *testing.T) {
	// Enable subprocess mocking (for ffx)
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	targetPath := cachePrefix + "/target/path"

	tempDir := t.TempDir()
	testcase := filepath.Join(tempDir, "testcase")
	touchRandomFile(t, testcase)
	if err := c.Put(testcase, targetPath); err != nil {
		t.Fatalf("failed to put artifact: %s", err)
	}

	// Should fail if the input doesn't exist
	if _, err := c.FfxRun("", "fuzz", "try", "url", invalidPath); err == nil {
		t.Fatalf("unexpectedly succeeded trying an invalid file")
	}

	// Therefore it should succeed iff the translation was done
	if _, err := c.FfxRun("", "fuzz", "try", "url",
		path.Join(targetPath, "testcase")); err != nil {
		t.Fatalf("error running ffx command: %s", err)
	}
}

func TestFfxRunWithOutputDirectory(t *testing.T) {
	// Enable subprocess mocking (for ffx)
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	dir := t.TempDir()
	if _, err := c.FfxRun(dir, "fuzz", "run", "url"); err != nil {
		t.Fatalf("error running ffx command: %s", err)
	}

	if !fileExists(filepath.Join(dir, "artifacts", "crash-1312")) {
		t.Fatalf("artifact not found in output dir %s", dir)
	}
}

func TestFfxRunWithoutFfx(t *testing.T) {
	// Enable subprocess mocking (for ffx)
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()
	brokenBuild := c.build.(*mockBuild)
	brokenBuild.paths["ffx"] = invalidPath

	if _, err := c.FfxRun("", "fuzz", "list"); err == nil {
		t.Fatalf("expected failure calling missing ffx but succeeded")
	}
}

func TestFfxRunInvalidCommand(t *testing.T) {
	// Enable subprocess mocking (for ffx)
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	if _, err := c.FfxRun("", "daemon", "pet"); err == nil {
		t.Fatalf("unexpectedly succeeded running invalid ffx command")
	}
}

func TestSSHInvalidCommand(t *testing.T) {
	c, _ := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	cmd := c.Command("LOAD", `"*",8`)
	if err := cmd.Run(); err == nil || err.(*InstanceCmdError).ReturnCode != 127 {
		t.Fatalf("expected command not found but got: %s", err)
	}
}

func TestSSHCommandWithConnectionFailures(t *testing.T) {
	// Should recover with a few failures
	c, _ := getFakeSSHConnector(t, sshReconnectCount-1)
	defer c.Cleanup()

	cmd := c.Command("echo", "a few")
	if err := cmd.Run(); err != nil {
		t.Fatalf("error running remote command: %s", err)
	}

	// Should abort after repeated failures
	c2, _ := getFakeSSHConnector(t, sshReconnectCount)
	defer c2.Close()

	cmd = c2.Command("echo", "too many")
	if err := cmd.Run(); err == nil {
		t.Fatalf("missing expected error running remote command")
	}
}

func TestSSHIsDir(t *testing.T) {
	// Only supported for cache paths
	remotePath := cachePrefix + "/root"

	c, fs := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	elements := []*fakeFile{
		{name: remotePath + "/subdir", isDir: true},
		{name: remotePath + "/subdir/file", content: ":D"}}
	setUpRemoteFilesystem(t, c, fs, elements)

	for _, file := range elements {
		isDir, err := c.IsDir(file.name)
		if err != nil {
			t.Fatalf("error stating file: %s", err)
		}
		if isDir != file.isDir {
			t.Fatalf("unexpected IsDir return for %s: %t", file.name, isDir)
		}
	}
}

func TestSSHRmDir(t *testing.T) {
	for _, remotePath := range []string{"/some/dir", cachePrefix + "/some/dir"} {
		t.Run(remotePath, func(t *testing.T) {
			c, fs := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			filename := remotePath + "/subdir/file"
			elements := []*fakeFile{
				{name: remotePath, isDir: true},
				{name: remotePath + "/subdir", isDir: true},
				{name: filename, content: ":D"}}
			setUpRemoteFilesystem(t, c, fs, elements)

			if err := c.RmDir(remotePath + "/subdir"); err != nil {
				t.Fatalf("error calling rmdir: %s", err)
			}

			expectRemoteFileAbsent(t, c, fs, filename)
			expectRemoteFileAbsent(t, c, fs, remotePath+"/subdir")
			expectRemoteFilePresent(t, c, fs, remotePath)
		})
	}
}

func TestSSHGet(t *testing.T) {
	for _, filename := range []string{"/testfile", cachePrefix + "/testfile"} {
		t.Run(filename, func(t *testing.T) {
			c, fs := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			tmpDir := t.TempDir()

			testFile := &fakeFile{name: filename, content: "test file contents"}
			setUpRemoteFilesystem(t, c, fs, []*fakeFile{testFile})

			if err := c.Get(filename, tmpDir); err != nil {
				t.Fatalf("error getting file: %s", err)
			}

			got, err := os.ReadFile(path.Join(tmpDir, path.Base(testFile.name)))
			if err != nil {
				t.Fatalf("error reading fetched file: %s", err)
			}

			if diff := cmp.Diff(testFile.content, string(got)); diff != "" {
				t.Fatalf("fetched file has unexpected content (-want +got):\n%s", diff)
			}
		})
	}
}

func TestSSHGetLiveCorpus(t *testing.T) {
	// Enable subprocess mocking (for ffx)
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	c, fs := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	tmpDir := t.TempDir()

	remotePath := cachePrefix + "/some/dir"
	files := []*fakeFile{
		{name: remotePath + "/" + liveCorpusMarkerName, content: "fuzzer_url"},
		{name: remotePath + "/extra", content: "extra"}}
	setUpRemoteFilesystem(t, c, fs, files)

	if err := c.Get(remotePath+"/*", tmpDir); err != nil {
		t.Fatalf("error getting file: %s", err)
	}

	got := readDirRecursive(t, tmpDir)
	if diff := cmp.Diff([]string{"a", "b", "extra"}, got); diff != "" {
		t.Fatalf("fetched dir has unexpected content (-want +got):\n%s", diff)
	}
}

func TestSSHGetLiveCorpusInSubdir(t *testing.T) {
	// Enable subprocess mocking (for ffx)
	ExecCommand = mockCommand
	defer func() { ExecCommand = exec.Command }()

	c, fakeFs := getFakeSSHConnector(t, 0)
	defer c.Cleanup()

	tmpDir := t.TempDir()

	remotePath := cachePrefix + "/some/dir"
	files := []*fakeFile{
		{name: remotePath + "/inner/innerfile", content: "bonus"},
		{name: remotePath + "/inner/" + liveCorpusMarkerName, content: "fuzzer_url"},
		{name: remotePath + "/outerfile", content: "extra"}}
	setUpRemoteFilesystem(t, c, fakeFs, files)

	if err := c.Get(remotePath+"/*", tmpDir); err != nil {
		t.Fatalf("error getting file: %s", err)
	}

	got := readDirRecursive(t, tmpDir)
	want := []string{"inner/a", "inner/b", "inner/innerfile", "outerfile"}
	if diff := cmp.Diff(want, got); diff != "" {
		t.Fatalf("fetched dir has unexpected content (-want +got):\n%s", diff)
	}
}

func TestSSHGetNonexistentSourceFile(t *testing.T) {
	for _, filename := range []string{"/testfile", cachePrefix + "/testfile"} {
		t.Run(filename, func(t *testing.T) {
			c, _ := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			tmpDir := t.TempDir()

			if err := c.Get(filename, tmpDir); err == nil {
				t.Fatal("expected error but succeeded")
			}
		})
	}
}

func TestSSHGetToNonexistentDestDir(t *testing.T) {
	for _, filename := range []string{"/testfile", cachePrefix + "/testfile"} {
		t.Run(filename, func(t *testing.T) {
			c, _ := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			tmpDir := t.TempDir()

			if err := c.Get(filename, filepath.Join(tmpDir, "nope")); err == nil {
				t.Fatal("expected error but succeeded")
			}
		})
	}
}

func TestSSHPut(t *testing.T) {
	for _, remotePath := range []string{"/some/dir", cachePrefix + "/some/dir"} {
		t.Run(remotePath, func(t *testing.T) {
			c, fs := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			tmpDir := t.TempDir()

			tmpFile := path.Join(tmpDir, "testfile")
			fileContents := "test file contents"

			if err := os.WriteFile(tmpFile, []byte(fileContents), 0o600); err != nil {
				t.Fatalf("error writing local file: %s", err)
			}

			files := []*fakeFile{{name: remotePath, isDir: true}}
			setUpRemoteFilesystem(t, c, fs, files)

			if err := c.Put(tmpFile, remotePath); err != nil {
				t.Fatalf("error putting file: %s", err)
			}

			expectRemoteFileWithContent(t, c, fs,
				path.Join(remotePath, filepath.Base(tmpFile)), fileContents)
		})
	}
}

func TestSSHGetGlob(t *testing.T) {
	for _, remotePath := range []string{"/subdir", cachePrefix + "/subdir"} {
		t.Run(remotePath, func(t *testing.T) {
			c, fs := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			tmpDir := t.TempDir()

			testFiles := []*fakeFile{
				{name: remotePath + "/a", content: "apple"},
				{name: remotePath + "/b", content: "banana"},
				{name: remotePath + "/j", content: "jabuticaba"},
			}

			// Add a fake directory entry so globbing works correctly
			entries := append(testFiles, &fakeFile{name: "/subdir", isDir: true})
			setUpRemoteFilesystem(t, c, fs, entries)

			if err := c.Get(remotePath+"/*", tmpDir); err != nil {
				t.Fatalf("error running remote command: %s", err)
			}

			for _, testFile := range testFiles {
				got, err := os.ReadFile(path.Join(tmpDir, filepath.Base(testFile.name)))
				if err != nil {
					t.Fatalf("error reading fetched file: %s", err)
				}

				if diff := cmp.Diff(testFile.content, string(got)); diff != "" {
					t.Fatalf("fetched file has unexpected content (-want +got):\n%s", diff)
				}
			}
		})
	}
}

func TestSSHPutGlob(t *testing.T) {
	for _, remotePath := range []string{"/some/dir", cachePrefix + "/some/dir"} {
		t.Run(remotePath, func(t *testing.T) {
			c, fs := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			tmpDir := t.TempDir()

			testFiles := []*fakeFile{
				{name: "a", content: "apple"},
				{name: "b", content: "banana"},
				{name: "j", content: "jabuticaba"},
			}

			for _, testFile := range testFiles {
				tmpFile := path.Join(tmpDir, testFile.name)
				if err := os.WriteFile(tmpFile, []byte(testFile.content), 0o600); err != nil {
					t.Fatalf("error writing local file: %s", err)
				}
			}

			files := []*fakeFile{{name: remotePath, isDir: true}}
			setUpRemoteFilesystem(t, c, fs, files)

			if err := c.Put(path.Join(tmpDir, "*"), remotePath); err != nil {
				t.Fatalf("error putting file: %s", err)
			}

			for _, testFile := range testFiles {
				expectRemoteFileWithContent(t, c, fs, path.Join(remotePath, testFile.name),
					testFile.content)
			}
		})
	}
}

func TestSSHGetDir(t *testing.T) {
	for _, remotePath := range []string{"/x", cachePrefix + "/x"} {
		t.Run(remotePath, func(t *testing.T) {
			c, fs := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			// Set up remote file structure

			testFiles := []*fakeFile{
				{name: remotePath + "/outer/a", content: "apple"},
				{name: remotePath + "/outer/inner/b", content: "banana"},
				{name: remotePath + "/outer/inner/j", content: "jabuticaba"},
			}

			testDirs := []*fakeFile{
				{name: remotePath, isDir: true},
				{name: remotePath + "/outer", isDir: true},
				{name: remotePath + "/outer/inner", isDir: true},
			}

			entries := append(testFiles, testDirs...)
			setUpRemoteFilesystem(t, c, fs, entries)

			// Get /outer (contains file and subdirectory)

			tmpDir := t.TempDir()

			srcDir := remotePath + "/outer"
			if err := c.Get(srcDir, tmpDir); err != nil {
				t.Fatalf("error getting dir: %s", err)
			}

			for _, testFile := range testFiles {
				relPath := strings.TrimPrefix(testFile.name, path.Dir(srcDir))
				got, err := os.ReadFile(path.Join(tmpDir, relPath))
				if err != nil {
					t.Fatalf("error reading fetched file: %s", err)
				}

				if diff := cmp.Diff(testFile.content, string(got)); diff != "" {
					t.Fatalf("fetched file has unexpected content (-want +got):\n%s", diff)
				}
			}

			// Get /outer/inner (contains files)

			tmpDir = t.TempDir()

			srcDir = remotePath + "/outer/inner"
			if err := c.Get(srcDir, tmpDir); err != nil {
				t.Fatalf("error getting dir: %s", err)
			}

			for _, testFile := range testFiles {
				relName := strings.TrimPrefix(testFile.name, path.Dir(srcDir))
				got, err := os.ReadFile(path.Join(tmpDir, relName))

				if !strings.HasPrefix(testFile.name, srcDir) {
					if err == nil {
						t.Fatalf("unexpected file retrieved: %q", testFile.name)
					}
				} else {
					if err != nil {
						t.Fatalf("error reading fetched file: %s", err)
					}

					if diff := cmp.Diff(testFile.content, string(got)); diff != "" {
						t.Fatalf("fetched file has unexpected content (-want +got):\n%s", diff)
					}
				}
			}
		})
	}
}

func TestSSHPutDir(t *testing.T) {
	for _, remotePath := range []string{"/some/dir", cachePrefix + "/some/dir"} {
		t.Run(remotePath, func(t *testing.T) {
			tmpDir := t.TempDir()

			testFiles := []*fakeFile{
				{name: "/outer/a", content: "apple"},
				{name: "/outer/inner/b", content: "banana"},
				{name: "/outer/inner/j", content: "jabuticaba"},
			}

			testDirs := []*fakeFile{
				{name: "/outer", isDir: true},
				{name: "/outer/inner", isDir: true},
			}

			for _, testDir := range testDirs {
				newDir := path.Join(tmpDir, testDir.name)
				if err := os.Mkdir(newDir, 0o700); err != nil {
					t.Fatalf("error creating local dir: %s", err)
				}
			}

			for _, testFile := range testFiles {
				tmpFile := path.Join(tmpDir, testFile.name)
				if err := os.WriteFile(tmpFile, []byte(testFile.content), 0o600); err != nil {
					t.Fatalf("error writing local file: %s", err)
				}
			}

			// Put /outer (contains file and subdirectory)

			c, fs := getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			files := []*fakeFile{{name: remotePath, isDir: true}}
			setUpRemoteFilesystem(t, c, fs, files)

			if err := c.Put(path.Join(tmpDir, "outer"), remotePath); err != nil {
				t.Fatalf("error putting dir: %s", err)
			}

			for _, testFile := range testFiles {
				expectRemoteFileWithContent(t, c, fs, path.Join(remotePath, testFile.name),
					testFile.content)
			}

			// Put /outer/inner (contains files)

			c, fs = getFakeSSHConnector(t, 0)
			defer c.Cleanup()

			setUpRemoteFilesystem(t, c, fs, files)

			srcDir := "/outer/inner"
			if err := c.Put(path.Join(tmpDir, srcDir), remotePath); err != nil {
				t.Fatalf("error putting dir: %s", err)
			}

			for _, testFile := range testFiles {
				relName := strings.TrimPrefix(testFile.name, path.Dir(srcDir))
				remotePath := path.Join(remotePath, relName)

				if strings.HasPrefix(testFile.name, srcDir) {
					expectRemoteFileWithContent(t, c, fs, remotePath, testFile.content)
				} else {
					expectRemoteFileAbsent(t, c, fs, remotePath)
				}
			}
		})
	}
}

// Helper functions:

func getFakeSSHConnector(t *testing.T, failureCount uint) (*SSHConnector, *fakeSftp) {
	glog.Info("Starting local SSH server...")

	conn, errCh, fakeFs, err := startLocalSSHServer(failureCount)
	if err != nil {
		t.Fatalf("error starting local server: %s", err)
	}

	conn.reconnectInterval = 1 * time.Millisecond

	build, _ := newMockBuild()
	conn.build = build

	// Monitor for server errors
	go func() {
		for err := range errCh {
			t.Errorf("error from local SSH server: %s", err)
		}
	}()

	return conn, fakeFs
}

func expectRemoteFileWithContent(t *testing.T, conn *SSHConnector,
	fs *fakeSftp, name string, content string) {
	if isCachePath(name) {
		got := readFileFromCache(t, conn, name)
		if diff := cmp.Diff(content, got); diff != "" {
			t.Fatalf("uploaded file has unexpected content (-want +got):\n%s", diff)
		}
		return
	}

	for _, f := range fs.files {
		if f.name == name {
			if diff := cmp.Diff(content, f.content); diff != "" {
				t.Fatalf("uploaded file has unexpected content (-want +got):\n%s", diff)
			}
			return
		}
	}
	t.Fatalf("uploaded file not found in expected location")
}

func expectRemoteFileAbsent(t *testing.T, conn *SSHConnector,
	fs *fakeSftp, path string) {
	if isCachePath(path) {
		hostPath, err := conn.cacheToHostPath(path)
		if err != nil {
			t.Fatalf("error looking up cache path %s: %s", path, err)
		}
		if fileExists(hostPath) {
			t.Fatalf("unexpected remote file exists: %q", path)
		}
		return
	}

	if _, err := fs.getFile(path); err == nil {
		t.Fatalf("unexpected remote file exists: %q", path)
	}
}

func expectRemoteFilePresent(t *testing.T, conn *SSHConnector,
	fs *fakeSftp, path string) {
	if isCachePath(path) {
		hostPath, err := conn.cacheToHostPath(path)
		if err != nil {
			t.Fatalf("error looking up cache path %s: %s", path, err)
		}
		if !fileExists(hostPath) {
			t.Fatalf("expected remote file doesn't exist: %q", path)
		}
		return
	}

	if _, err := fs.getFile(path); err != nil {
		t.Fatalf("expected remote file doesn't exist: %q", path)
	}
}

// List all files in a directory, recursively. Returns paths relative to the root path.
func readDirRecursive(t *testing.T, root string) []string {
	var files []string
	walker := fs.Walk(root)
	for walker.Step() {
		if err := walker.Err(); err != nil {
			t.Fatalf("error while walking: %s", err)
		}
		if walker.Stat().IsDir() {
			continue
		}

		relPath, err := filepath.Rel(root, walker.Path())
		if err != nil {
			t.Fatalf("error taking relpath: %s", err)
		}
		files = append(files, relPath)
	}

	return files
}

// Set an initial state of the (fake) remote filesystem
func setUpRemoteFilesystem(t *testing.T, conn *SSHConnector,
	fs *fakeSftp, files []*fakeFile) {

	// Need to ensure TmpDir is initialized
	if err := conn.Connect(); err != nil {
		t.Fatalf("error connecting: %s", err)
	}

	fs.files = []*fakeFile{}
	for _, file := range files {
		if !isCachePath(file.name) {
			fs.files = append(fs.files, file)
		} else if !file.isDir {
			writeFileToCache(t, conn, file.name, file.content)
		}
	}
}

// Methods for directly manipulating files inside the cache, to set up/check
// its state when testing
func readFileFromCache(t *testing.T, conn *SSHConnector, path string) string {
	hostPath, err := conn.cacheToHostPath(path)
	if err != nil {
		t.Fatalf("error looking up cache path %s: %s", path, err)
	}
	data, err := os.ReadFile(hostPath)
	if err != nil {
		t.Fatalf("error reading file %s from cache: %s", hostPath, err)
	}
	return string(data)
}

func writeFileToCache(t *testing.T, conn *SSHConnector, path, content string) {
	hostPath, err := conn.cacheToHostPath(path)
	if err != nil {
		t.Fatalf("error looking up cache path %s: %s", path, err)
	}

	// Make any enclosing dirs automatically
	if err := os.MkdirAll(filepath.Dir(hostPath), os.ModeDir|0o700); err != nil {
		t.Fatalf("error making dir(s) for cache path %s: %s", hostPath, err)
	}

	if err := os.WriteFile(hostPath, []byte(content), 0o600); err != nil {
		t.Fatalf("error writing file %s to cache: %s", hostPath, err)
	}
}
