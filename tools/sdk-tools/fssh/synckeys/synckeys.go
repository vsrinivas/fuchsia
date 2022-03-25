// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package synckeys

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"os/user"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	sshDirName                   = ".ssh"
	fuchsiaAuthorizedKeyFilename = "fuchsia_authorized_keys"
	fuchsiaPrivateSSHKeyFilename = "fuchsia_ed25519"
	sshKeygenExecName            = "ssh-keygen"
	pathNotExistErrMsg           = "No such file or directory"
	differentKeysErrMsg          = `keys on local and remote are not the same.
Please ensure the files %q and %q in %q on your local machine and in %q on %q are the same.
You can backup and delete the remote keys on %q and re-run fssh sync-keys which will copy the local keys over to remote.`
)

var fuchsiaKeyFilenames = []string{
	fuchsiaAuthorizedKeyFilename,
	fuchsiaPrivateSSHKeyFilename,
}

type Workstation struct {
	KeysExist  bool
	KeyPaths   []string
	TempKeyDir string
	HomeDir    string
	SSHDir     string
	Hostname   string
}

// Fuchsia() syncs the Fuchsia SSH keys between the local and specified remote.
func Fuchsia(ctx context.Context, remoteHostname string) error {
	logger.Debugf(ctx, "Running SyncKeys with remote %q...", remoteHostname)

	logger.Debugf(ctx, "Checking for local Fuchsia keys...")
	local, err := processLocalFuchsiaKeys(ctx, "")
	if err != nil {
		return fmt.Errorf("could not check for local Fuchsia keys: %w", err)
	}

	logger.Debugf(ctx, "Local keys exists: %t", local.KeysExist)
	logger.Debugf(ctx, "Local keys paths: %q", strings.Trim(strings.Join(local.KeyPaths, ", "), ", "))
	logger.Debugf(ctx, "Local home directory: %q", local.HomeDir)
	logger.Debugf(ctx, "Local SSH directory: %q", local.SSHDir)

	logger.Debugf(ctx, "Checking for remote Fuchsia keys...")
	remote, err := processRemoteFuchsiaKeys(ctx, runCommand, remoteHostname, "")
	if err != nil {
		return fmt.Errorf("could not process remote Fuchsia keys: %w", err)
	}
	defer os.RemoveAll(remote.TempKeyDir)
	logger.Debugf(ctx, "Remote keys exists: %t", remote.KeysExist)
	logger.Debugf(ctx, "Remote keys paths: %q", strings.Trim(strings.Join(remote.KeyPaths, ", "), ", "))
	logger.Debugf(ctx, "Remote keys local temporary directory: %q", remote.TempKeyDir)
	logger.Debugf(ctx, "Remote home directory: %q", remote.HomeDir)
	logger.Debugf(ctx, "Remote SSH directory: %q", remote.SSHDir)

	if !local.KeysExist && !remote.KeysExist {
		logger.Debugf(ctx, "Some/all Fuchsia keys missing from both remote and local.")
		if err := generateFuchsiaKeys(ctx, runCommand, local, remote); err != nil {
			return fmt.Errorf("could not generate Fuchsia keys: %w", err)
		}
	} else if local.KeysExist && remote.KeysExist {
		logger.Debugf(ctx, "Both local and remote Fuchsia keys present.")
		logger.Debugf(ctx, "Checking if local and remote Fuchsia keys are the same...")
		sameKeys, err := sameFuchsiaKeys(ctx, local.SSHDir, remote.TempKeyDir)
		if err != nil {
			return fmt.Errorf("could not check if remote and local key files are the same: %w", err)
		}
		if !sameKeys {
			return fmt.Errorf(differentKeysErrMsg,
				fuchsiaAuthorizedKeyFilename,
				fuchsiaPrivateSSHKeyFilename,
				local.SSHDir,
				remote.SSHDir,
				remote.Hostname,
				remote.Hostname,
			)
		}
		logger.Debugf(ctx, "Remote and local keys are the same.")
	} else if local.KeysExist && !remote.KeysExist {
		logger.Debugf(ctx, "Fuchsia keys present on local but not remote.")
		logger.Debugf(ctx, "Copying local Fuchsia keys to %q", remote.Hostname)
		if err := copyLocalKeysToRemote(ctx, runCommand, local.KeyPaths, remote.Hostname, remote.SSHDir); err != nil {
			return fmt.Errorf("could not copy local keys %q to directory %q on remote %q: %w",
				local.KeyPaths,
				remote.Hostname,
				remote.SSHDir,
				err,
			)
		}
		logger.Debugf(ctx, "Copied local keys to remote %q successfully.", remote.Hostname)
	} else if !local.KeysExist && remote.KeysExist {
		logger.Debugf(ctx, "Fuchsia keys present on remote but not local.")
		logger.Debugf(ctx, "Copying remote Fuchsia keys to local...")
		var remotePaths []string
		for _, filename := range fuchsiaKeyFilenames {
			remotePaths = append(remotePaths, filepath.Join(remote.TempKeyDir, filename))
		}
		if err = copyPathsToDir(remotePaths, local.SSHDir); err != nil {
			return fmt.Errorf("could not copy remote keys to local: %w", err)
		}
		logger.Debugf(ctx, "Copied remote Fuchsia keys from %q to local successfully.", remote.Hostname)
	}

	logger.Debugf(ctx, "Keys synced without error.")
	return nil
}

func processLocalFuchsiaKeys(ctx context.Context, homeDir string) (*Workstation, error) {
	// If home dir is not provided, get the user's home dir.
	if homeDir == "" {
		var err error
		homeDir, err = getHomeDir()
		if err != nil {
			return nil, fmt.Errorf("could not get local home directory: %w", err)
		}
	}
	sshDir := filepath.Join(homeDir, sshDirName)
	paths := []string{}
	for _, filename := range fuchsiaKeyFilenames {
		paths = append(paths, filepath.Join(sshDir, filename))
	}
	exist := checkLocalFuchsiaKeys(ctx, paths)

	return &Workstation{
		KeysExist: exist,
		HomeDir:   homeDir,
		SSHDir:    sshDir,
		KeyPaths:  paths,
	}, nil
}

func processRemoteFuchsiaKeys(ctx context.Context, runCommand runner, hostname string, homeDir string) (*Workstation, error) {
	if homeDir == "" {
		out, err := runCommand(ctx, "ssh", []string{
			hostname,
			"echo $HOME",
		})
		if err != nil {
			return nil, fmt.Errorf("could not get remote home directory: %w", err)
		}
		homeDir = strings.TrimSpace(out)
	}
	sshDir := filepath.Join(homeDir, sshDirName)

	paths := []string{}
	for _, filename := range fuchsiaKeyFilenames {
		paths = append(paths, filepath.Join(sshDir, filename))
	}
	tempKeyDir, err := ioutil.TempDir(os.TempDir(), "fssh-temp-remote-keys-")
	if err != nil {
		return nil, fmt.Errorf("could not create local temporary folder for remote SSH keys: %w", err)
	}
	var exists bool
	if err = copyRemoteKeysToLocal(ctx, runCommand, tempKeyDir, hostname, paths); err != nil {
		// copyRemoteKeysToLocal will throw an "path doesn't exist" error if
		// any of the files are not present.
		if strings.Contains(err.Error(), pathNotExistErrMsg) {
			exists = false
		} else {
			err := fmt.Errorf("could not check for remote Fuchsia auth files: %w", err)
			// Remote temp dir that will not be used.
			if cleanupErr := os.RemoveAll(tempKeyDir); cleanupErr != nil {
				err = fmt.Errorf("%v. Warning: could not cleanup dir %q: %w", err, tempKeyDir, cleanupErr)
			}
			return nil, err
		}
	} else {
		exists = true
	}
	return &Workstation{
		KeysExist:  exists,
		KeyPaths:   paths,
		TempKeyDir: tempKeyDir,
		HomeDir:    homeDir,
		SSHDir:     sshDir,
		Hostname:   hostname,
	}, nil
}

func generateFuchsiaKeys(ctx context.Context, runCommand runner, local *Workstation, remote *Workstation) error {
	// Check for existing private keys
	localPrivateKeyPath := filepath.Join(local.SSHDir, fuchsiaPrivateSSHKeyFilename)
	localPrivateKeyExists := localFileExists(localPrivateKeyPath)
	remotePrivateKeyPath := filepath.Join(remote.TempKeyDir, fuchsiaPrivateSSHKeyFilename)
	remotePrivateKeyExists := localFileExists(remotePrivateKeyPath)
	// If both local and remote private keys exist, ensure contents is the same
	if localPrivateKeyExists && remotePrivateKeyExists {
		same, err := filesHaveSameContents(ctx, localPrivateKeyPath, remotePrivateKeyPath)
		if err != nil {
			return fmt.Errorf("could not check if remote private key (copied locally, %q) and local private key (%q) are the same: %w", remotePrivateKeyPath, localPrivateKeyPath, err)
		}
		if !same {
			return fmt.Errorf(differentKeysErrMsg,
				fuchsiaAuthorizedKeyFilename,
				fuchsiaPrivateSSHKeyFilename,
				local.SSHDir,
				remote.SSHDir,
				remote.Hostname,
				remote.Hostname,
			)
		}
	} else if remotePrivateKeyExists {
		// If private key only exists on remote, copy it locally
		if err := copyPathsToDir([]string{remotePrivateKeyPath}, local.SSHDir); err != nil {
			return fmt.Errorf("could not copy remote private key (copied locally to %q) to local SSH dir %q: %w", remotePrivateKeyPath, local.SSHDir, err)
		}
	}

	// At this point, if any Fuchsia private key exists:
	//   1. the local private key is the same as the private key on the remote
	//   2. the private key is in the local SSH dir
	// This means we can generate any additionally needed keys locally and then
	// copy them over to the remote to sync the keys.
	if err := generateLocalKeys(ctx, runCommand, local.SSHDir); err != nil {
		return fmt.Errorf("could not generate new SSH keys: %w", err)
	}
	if err := copyLocalKeysToRemote(ctx, runCommand, local.KeyPaths, remote.Hostname, remote.SSHDir); err != nil {
		return fmt.Errorf("could note copy local keys to remote: %w", err)
	}

	return nil
}

func checkLocalFuchsiaKeys(ctx context.Context, paths []string) bool {
	var missingFiles []string
	for _, path := range paths {
		if !localFileExists(path) {
			missingFiles = append(missingFiles, path)
		}
	}
	if len(missingFiles) > 0 {
		missingFilesMsg := strings.Trim(strings.Join(missingFiles, ", "), ", ")
		logger.Debugf(ctx, "Found some local Fuchsia key files but the following key files were missing: %q", missingFilesMsg)
	}

	return len(missingFiles) == 0
}

func generateLocalKeys(ctx context.Context, runCommand runner, sshDir string) error {
	localPrivateKeyPath := filepath.Join(sshDir, fuchsiaPrivateSSHKeyFilename)
	// If no existing private key exists, generate a new one.
	if !localFileExists(localPrivateKeyPath) {
		if err := generatePrivateKey(ctx, runCommand, localPrivateKeyPath); err != nil {
			return fmt.Errorf("could not generate a new private SSH key: %w", err)
		}
	}

	// Generate a authorized key file
	localAuthKeyPath := filepath.Join(sshDir, fuchsiaAuthorizedKeyFilename)
	if !localFileExists(localAuthKeyPath) {
		if err := generatePublicKeyfile(ctx, runCommand, localPrivateKeyPath, localAuthKeyPath); err != nil {
			return fmt.Errorf("could not generate local authorized key file %q: %w", localAuthKeyPath, err)
		}
	}
	return nil
}

func generatePrivateKey(ctx context.Context, runCommand runner, dst string) error {
	// Generating private/public key pair
	if _, err := runCommand(ctx, sshKeygenExecName, []string{
		"-N",
		"",
		"-t",
		"ed25519",
		"-f",
		dst,
	}); err != nil {
		return fmt.Errorf("could not run %q : %w", sshKeygenExecName, err)
	}
	return nil
}

func generatePublicKeyfile(ctx context.Context, runCommand runner, privateKeyPath string, dst string) error {
	// Generating authorized key file
	authorizedKeyFileContents, err := runCommand(ctx, sshKeygenExecName, []string{
		"-y",
		"-f",
		privateKeyPath,
	})
	if err != nil {
		return fmt.Errorf("could not run %q : %w", sshKeygenExecName, err)
	}
	// Write authorized key file
	if err = ioutil.WriteFile(dst, []byte(authorizedKeyFileContents), 0400); err != nil {
		return fmt.Errorf("could not write authorized key file %q : %w", dst, err)
	}
	return nil
}

func sameFuchsiaKeys(ctx context.Context, dir1 string, dir2 string) (bool, error) {
	for i := range fuchsiaKeyFilenames {
		path1 := filepath.Join(dir1, fuchsiaKeyFilenames[i])
		path2 := filepath.Join(dir2, fuchsiaKeyFilenames[i])
		same, err := filesHaveSameContents(ctx, path1, path2)
		if err != nil {
			return false, fmt.Errorf("could not check %q and %q to see if they have the same contents: %w", path1, path2, err)
		}
		if !same {
			logger.Warningf(ctx, "%s != %s", path1, path2)
			return false, nil
		}
	}
	return true, nil
}

// Checks that the 2 files have the same contents.
// The comparison is line based and empty lines are ignored.
func filesHaveSameContents(ctx context.Context, path1 string, path2 string) (bool, error) {
	contents1, err := ioutil.ReadFile(path1)
	if err != nil {
		return false, fmt.Errorf("could not open file %q: %w'", path1, err)
	}
	contents2, err := ioutil.ReadFile(path2)
	if err != nil {
		return false, fmt.Errorf("could not open file %q: %w'", path2, err)
	}
	nonEmptyLineSplitter := func(c rune) bool {
		return c == '\n'
	}
	lines1 := strings.FieldsFunc(string(contents1), nonEmptyLineSplitter)
	lines2 := strings.FieldsFunc(string(contents2), nonEmptyLineSplitter)

	same := len(lines1) == len(lines2)
	if same {
		for i, line := range lines1 {
			if line != lines2[i] {
				same = false
				break
			}
		}
	}
	if !same {
		logger.Warningf(ctx, "%s != %s", path1, path2)
		logger.Warningf(ctx, "path1 contents: %q", string(contents1))
		logger.Warningf(ctx, "path2 contents: %q", string(contents2))
	}
	return same, nil
}

func copyLocalKeysToRemote(ctx context.Context, runCommand runner, localPaths []string, remote string, remoteDir string) error {
	var sshArgs []string
	for _, path := range localPaths {
		sshArgs = append(sshArgs, path)
	}

	if remote == "" {
		sshArgs = append(sshArgs, remoteDir)
	} else {
		sshArgs = append(sshArgs, fmt.Sprintf("%s:%s", remote, remoteDir))
	}
	if _, err := runCommand(ctx, "scp", sshArgs); err != nil {
		return fmt.Errorf("could not copy local SSH keys to remote %q at path %q: %w", remote, remoteDir, err)
	}

	return nil
}

func copyRemoteKeysToLocal(ctx context.Context, runCommand runner, localDir string, remote string, remotePaths []string) error {
	var sshArgs []string
	for _, path := range remotePaths {
		arg := path
		if remote != "" {
			arg = fmt.Sprintf("%s:%s", remote, path)
		}
		sshArgs = append(sshArgs, arg)
	}
	sshArgs = append(sshArgs, localDir)
	if _, err := runCommand(ctx, "scp", sshArgs); err != nil {
		return fmt.Errorf("could not copy remote SSH keys on %q to local path %q: %w", remote, localDir, err)
	}

	return nil
}

type runner func(context.Context, string, []string) (string, error)

func runCommand(ctx context.Context, execName string, args []string) (string, error) {
	path, err := findOnPath(execName)
	if err != nil {
		return "", err
	}
	logger.Debugf(ctx, "Running command %q", fmt.Sprintf("%s %s", path, strings.Join(args, " ")))
	cmd := exec.Command(path, args...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf(
			"could not run %s: %q",
			path,
			string(out),
		)
	}
	logger.Debugf(ctx, "command output: %q", string(out))
	return string(out), nil
}

// findOnPath finds a executable with the name `execName` in the directories
/// specified by the PATH environment variable.
func findOnPath(execName string) (string, error) {
	path, err := exec.LookPath(execName)
	if err != nil {
		return "", err
	}
	return path, nil
}

func getHomeDir() (string, error) {
	usr, err := user.Current()
	if err != nil {
		return "", err
	}
	return usr.HomeDir, nil
}

func localFileExists(filename string) bool {
	info, err := os.Stat(filename)
	if os.IsNotExist(err) {
		return false
	}
	return !info.IsDir()
}

// sameBoolValue checks to see if all boolean values are the same.
// returns true if the are all the same false if not.
func sameBoolValue(bools ...bool) bool {
	if len(bools) == 0 {
		return false
	}
	first := bools[0]
	for _, b := range bools {
		if b != first {
			return false
		}
	}
	return true
}

func copyPathsToDir(paths []string, dir string) error {
	for _, path := range paths {
		filename := filepath.Base(path)
		dst, err := os.Create(filepath.Join(dir, filename))
		if err != nil {
			return err
		}
		defer dst.Close()

		src, err := os.Open(path)
		if err != nil {
			return err
		}
		defer src.Close()

		if _, err = io.Copy(dst, src); err != nil {
			return err
		}
	}
	return nil
}
