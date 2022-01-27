// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package synckeys

import (
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

func TestProcessLocalFuchsiaKeys(t *testing.T) {
	emptyDir, err := ioutil.TempDir(t.TempDir(), "fssh-test-")
	if err != nil {
		t.Fatalf("could not create temporary folder: %s", err)
	}
	defer os.RemoveAll(emptyDir)
	var tests = []struct {
		homeDir             string
		expectedWorkstation *Workstation
	}{
		{
			homeDir: emptyDir,
			expectedWorkstation: &Workstation{
				KeysExist: false,
				HomeDir:   emptyDir,
				SSHDir:    filepath.Join(emptyDir, sshDirName),
				KeyPaths: []string{
					filepath.Join(emptyDir, sshDirName, fuchsiaAuthorizedKeyFilename),
					filepath.Join(emptyDir, sshDirName, fuchsiaPrivateSSHKeyFilename),
				},
			},
		},
	}
	for _, test := range tests {
		ctx := context.Background()
		w, err := processLocalFuchsiaKeys(ctx, test.homeDir)
		if err != nil {
			t.Errorf("processLocalFuchsiaKeys(): got error %s, expected no error", err)
		}
		if diff := cmp.Diff(test.expectedWorkstation, w); diff != "" {
			t.Errorf("processLocalFuchsiaKeys(): %s", diff)
		}
	}
}

func TestGenerateFuchsiaKeys(t *testing.T) {
	tempDir := t.TempDir()
	localDir, err := ioutil.TempDir(tempDir, "fssh-test-")
	if err != nil {
		t.Fatalf("could not create temporary folder %s: %s", localDir, err)
	}
	defer os.RemoveAll(localDir)

	localSSHDir := filepath.Join(localDir, sshDirName)
	if err = os.Mkdir(localSSHDir, 0755); err != nil {
		t.Fatalf("could not create temporary folder %s: %s", localSSHDir, err)
	}

	remoteDir, err := ioutil.TempDir(tempDir, "fssh-test-")
	if err != nil {
		t.Fatalf("could not create temporary folder: %s", err)
	}
	defer os.RemoveAll(remoteDir)

	remoteSSHDir := filepath.Join(remoteDir, sshDirName)
	if err = os.Mkdir(remoteSSHDir, 0755); err != nil {
		t.Fatalf("could not create temporary folder %s: %s", remoteSSHDir, err)
	}

	var tests = []struct {
		local  *Workstation
		remote *Workstation
	}{
		{
			local: &Workstation{
				KeysExist: false,
				HomeDir:   localDir,
				SSHDir:    localSSHDir,
				KeyPaths: []string{
					filepath.Join(localSSHDir, fuchsiaAuthorizedKeyFilename),
					filepath.Join(localSSHDir, fuchsiaPrivateSSHKeyFilename),
				},
			},
			remote: &Workstation{
				KeysExist: false,
				Hostname:  "",
				HomeDir:   remoteDir,
				SSHDir:    remoteSSHDir,
				KeyPaths: []string{
					filepath.Join(remoteSSHDir, fuchsiaAuthorizedKeyFilename),
					filepath.Join(remoteSSHDir, fuchsiaPrivateSSHKeyFilename),
				},
			},
		},
	}
	for _, test := range tests {
		ctx := context.Background()
		if err := generateFuchsiaKeys(ctx, testRunCommand, test.local, test.remote); err != nil {
			t.Fatalf("error calling generateFuchsiaKeys: %s", err)
		}
	}
}

func TestProcessRemoteFuchsiaKeys(t *testing.T) {
	emptyDir, err := ioutil.TempDir(t.TempDir(), "fssh-test-")
	if err != nil {
		t.Fatalf("could not create temporary folder: %s", err)
	}
	defer os.RemoveAll(emptyDir)
	var tests = []struct {
		hostname            string
		homeDir             string
		expectedWorkstation *Workstation
	}{
		{
			hostname: "",
			homeDir:  emptyDir,
			expectedWorkstation: &Workstation{
				KeysExist: false,
				HomeDir:   emptyDir,
				SSHDir:    filepath.Join(emptyDir, sshDirName),
				KeyPaths: []string{
					filepath.Join(emptyDir, sshDirName, fuchsiaAuthorizedKeyFilename),
					filepath.Join(emptyDir, sshDirName, fuchsiaPrivateSSHKeyFilename),
				},
			},
		},
	}
	for _, test := range tests {
		ctx := context.Background()
		w, err := processRemoteFuchsiaKeys(ctx, testRunCommand, test.hostname, test.homeDir)
		defer os.RemoveAll(w.TempKeyDir)
		if err != nil {
			t.Errorf("processRemoteFuchsiaKeys(): got error %s, expected no error", err)
		}
		if diff := cmp.Diff(test.expectedWorkstation, w, cmpopts.IgnoreFields(Workstation{}, "TempKeyDir")); diff != "" {
			t.Errorf("processRemoteFuchsiaKeys(): %s", diff)
		}

	}
}

func TestCheckLocalFuchsiaKeys(t *testing.T) {
	tempDir := t.TempDir()
	failingKeyCheckTestDir, err := ioutil.TempDir(tempDir, "fssh-key-check-test-")
	if err != nil {
		t.Fatalf("could not create temporary folder: %s", err)
	}
	defer os.RemoveAll(failingKeyCheckTestDir)

	passingKeyCheckTestDir, err := ioutil.TempDir(tempDir, "fssh-key-check-test-")
	if err != nil {
		t.Fatalf("could not create temporary folder: %s", err)
	}
	defer os.RemoveAll(passingKeyCheckTestDir)
	authKeyPath := filepath.Join(passingKeyCheckTestDir, fuchsiaAuthorizedKeyFilename)
	if _, err = os.Create(authKeyPath); err != nil {
		t.Fatalf("could not create needed file %s: %s", authKeyPath, err)
	}

	privateKeyPath := filepath.Join(passingKeyCheckTestDir, fuchsiaPrivateSSHKeyFilename)
	if _, err = os.Create(privateKeyPath); err != nil {
		t.Fatalf("could not create needed file %s: %s", privateKeyPath, err)
	}
	var tests = []struct {
		localPaths     []string
		expectedResult bool
	}{
		{
			localPaths: []string{
				path.Join(failingKeyCheckTestDir, fuchsiaAuthorizedKeyFilename),
				path.Join(failingKeyCheckTestDir, fuchsiaPrivateSSHKeyFilename),
			},
			expectedResult: false,
		},
		{
			localPaths: []string{
				path.Join(passingKeyCheckTestDir, fuchsiaAuthorizedKeyFilename),
				path.Join(passingKeyCheckTestDir, fuchsiaPrivateSSHKeyFilename),
			},
			expectedResult: true,
		},
	}
	for _, test := range tests {
		ctx := context.Background()
		keysExist := checkLocalFuchsiaKeys(ctx, test.localPaths)
		if keysExist != test.expectedResult {
			t.Fatalf("checkLocalFuchsiaKeys() got %t, want %t", keysExist, test.expectedResult)
		}
	}
}

func TestGenerateLocalKeys(t *testing.T) {
	// Test setup
	tempDir, err := ioutil.TempDir(t.TempDir(), "fssh-gen-key-test-")
	if err != nil {
		t.Fatalf("could not create temporary folder: %s", err)
	}
	defer os.RemoveAll(tempDir)
	ctx := context.Background()
	err = generateLocalKeys(ctx, testRunCommand, tempDir)
	if err != nil {
		t.Fatalf("error calling generateLocalKeys: %s", err)
	}

	// Check for generated key file existence
	privateKeyPath := filepath.Join(tempDir, fuchsiaPrivateSSHKeyFilename)
	if !localFileExists(privateKeyPath) {
		t.Fatalf("expected file %s to exist but does not", privateKeyPath)
	}

	authorizedKeyPath := filepath.Join(tempDir, fuchsiaAuthorizedKeyFilename)
	if !localFileExists(authorizedKeyPath) {
		t.Fatalf("expected file %s to exist but does not", authorizedKeyPath)
	}
}

func TestFilesHaveSameContents(t *testing.T) {
	tempDir := t.TempDir()
	file1, err := writeTempFile(tempDir, []string{"This is file1\n"})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(file1)

	file1NoNewline, err := writeTempFile(tempDir, []string{"This is file1"})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(file1NoNewline)

	file1WithBlankLine, err := writeTempFile(tempDir, []string{"This is file1\n\n"})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(file1WithBlankLine)

	multiline, err := writeTempFile(tempDir, []string{"This is line1", "line2", "line3"})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(multiline)

	file2, err := writeTempFile(tempDir, []string{"This is file2", "\n"})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(file2)

	empty, err := writeTempFile(tempDir, []string{"\n"})
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(empty)

	tests := []struct {
		left     string
		right    string
		expected bool
	}{
		{
			left:     file1,
			right:    file1,
			expected: true,
		},
		{
			left:     file1,
			right:    file1NoNewline,
			expected: true,
		},
		{
			left:     file1,
			right:    file1WithBlankLine,
			expected: true,
		},
		{
			left:     file1NoNewline,
			right:    file1WithBlankLine,
			expected: true,
		},
		{
			left:     file1,
			right:    file2,
			expected: false,
		},
		{
			left:     multiline,
			right:    empty,
			expected: false,
		},
		{
			left:     empty,
			right:    multiline,
			expected: false,
		},
	}
	for i, test := range tests {
		t.Run(fmt.Sprintf("TestFilesHaveSameContents %d", i), func(t *testing.T) {
			ctx := context.Background()
			same, err := filesHaveSameContents(ctx, test.left, test.right)
			if err != nil {
				t.Fatal(err)
			}
			if same != test.expected {
				t.Fatalf("filesHaveSameContents() got %s, want %s", test.left, test.right)
			}
		})
	}
}

func writeTempFile(dir string, contents []string) (path string, err error) {
	f, err := ioutil.TempFile(dir, "fssh-test-")
	if err != nil {
		return "", fmt.Errorf("could not create temp testing file %s: %s", f.Name(), err)
	}
	defer f.Close()
	for _, content := range contents {
		written, err := f.WriteString(content)
		if err != nil || written == 0 {
			return "", fmt.Errorf("could not write %s to temp testing file %s: %s", content, f.Name(), err)
		}
	}
	return f.Name(), nil
}

func testRunCommand(_ context.Context, execName string, args []string) (string, error) {
	path, err := findOnPath(execName)
	if err != nil {
		return "", err
	}
	cmd := exec.Command(path, args...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf(
			"could not run %s: %s",
			path,
			string(out),
		)
	}
	return string(out), nil
}
