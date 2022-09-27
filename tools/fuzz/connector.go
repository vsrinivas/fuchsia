// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/golang/glog"
	"github.com/kr/fs"
	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

// A Connector is used to communicate with an instance
type Connector interface {
	// Connect establishes all necessary connections to the instance. It does
	// not need to be explicitly called, because the other Connector methods will
	// automatically connect if necessary, but may be called during initializiation.
	// It the connector is already connected, an error will be returned.
	Connect() error

	// Close closes any open connections to the instance. It is the client's
	// responsibility to call either Close() or Cleanup().
	Close()

	// This is called on Instance shutdown, to allow for the cleanup of any
	// temporary files or resources. It is the client's responsibility to call
	// Cleanup() when permanently done with the Connector. Once this is called,
	// the Connector can no longer be used.
	Cleanup()

	// Returns an InstanceCmd representing the command to be run on the instance. Only one
	// command should be active at a time.
	// TODO(fxbug.dev/47479): In some cases, we should be able to relax the above restriction
	Command(name string, args ...string) InstanceCmd

	// Copies targetSrc (may include globs) to hostDst, which is always assumed
	// to be a directory. Directories are copied recursively.
	Get(targetSrc, hostDst string) error

	// Copies hostSrc (may include globs) to targetDst, which is always assumed
	// to be a directory. Directories are copied recursively.
	Put(hostSrc, targetDst string) error

	// Retrieves a syslog from the instance, filtered to the given process ID
	GetSysLog(pid int) (string, error)

	// Makes an ffx call to the target and returns its combined stdout/err,
	// with its output artifacts/logs directed into to outputDir.
	//
	// This possibly makes sense as a separate Connector type, eventually, but:
	// - ffx relies on knowing the SSH connection details
	// - We currently only support one Connector per Instance
	FfxRun(outputDir string, args ...string) (string, error)

	// Returns an exec.Cmd object that can be used to make an ffx call.
	FfxCommand(outputDir string, args ...string) (*exec.Cmd, error)

	// Recursively deletes the given directory on the target.
	RmDir(targetPath string) error

	// Checks whether the provided path on the target is a directory.
	IsDir(targetPath string) (bool, error)
}

// An SSHConnector is a Connector that uses SSH/SFTP for transport, along with
// ffx for integrating with CFF fuzzers.
//
// The SSHConnector supports two types of paths to refer to files on the target
// device: real paths that will be passed directly to SFTP commands, and "cache
// paths" which map to a local emulated target filesystem. These cache paths
// are distinguished by a special prefix (so as to de-couple Connector logic
// from Fuzzer logic as much as possible).
//
// This local caching system is necessary because CFF fuzzers don't have a
// notion of a filesystem (inputs/outputs are passed via FIDL), but undercoat
// needs to present a libFuzzer-style interface (which is centered around file
// and directory paths).
//
// In other words, the cache exists to temporarily store fuzzer inputs and
// outputs when bridging the gap between the ClusterFuchsia API and the `ffx
// fuzz` API. For example, a testcase "pushed" by `put_data` will likely be
// needed by a subsequent call to `run_fuzzer`, but we can't know exactly how
// it's going to be used until the latter call is made, so we need to keep a
// copy of the testcase around and keep track of the (virtual) target path used
// to refer to it.
//
// Note: exported fields will be serialized to the handle
type SSHConnector struct {
	// Host can be any IP or hostname as accepted by net.Dial
	Host string
	Port int
	// Key is a path to the SSH private key that should be used for
	// authentication
	Key string

	build         Build
	ffxPath       string
	ffxIsolateDir string
	client        *ssh.Client
	sftpClient    *sftp.Client

	// Retry configuration; defaults only need to be overridden for testing
	reconnectInterval time.Duration

	// This tempdir is used for storing the local filesystem cache described
	// above, as well as the `ffxIsolateDir` supporting the ffx daemon for this
	// instance. It shares a lifetime with the enclosing Instance (created on
	// first ffx connection, deleted on `stop_instance`).
	TmpDir string
}

const sshReconnectCount = 6

// This is mutable/public only so it can be overridden during e2e tests
var DefaultSSHReconnectInterval = 15 * time.Second

// Within the emulated v2 target filesystem, some directories need to be mapped
// to the live corpus for a given fuzzer on the target. This is done by placing
// a specially-named marker file in that directory in the local cache,
// containing the URL of the fuzzer to link to.
//
// Note that we don't simply pre-fetch the live corpus into the cache because
// this may take a significant amount of time for large corpora and we don't
// want e.g. `run_fuzzer` to hit caller timeouts due to the extra transfer
// time.
const liveCorpusMarkerName = ".live_corpus"

func NewSSHConnector(build Build, host string, port int, key string,
	tmpDir string) *SSHConnector {
	return &SSHConnector{build: build, Host: host, Port: port, Key: key,
		TmpDir:            tmpDir,
		reconnectInterval: DefaultSSHReconnectInterval,
	}
}

func (c *SSHConnector) initializeFfx() (returnErr error) {
	// Resolve path to ffx tool
	paths, err := c.build.Path("ffx")
	if err != nil {
		return fmt.Errorf("no ffx tool found: %s", err)
	}
	c.ffxPath = paths[0]

	// If we fail after this point, mark ffx as uninitialized so initialization
	// can be re-attempted when called again as part of any higher-level retry
	// logic.
	defer func() {
		if returnErr != nil {
			c.ffxPath = ""
		}
	}()

	if err := c.initializeTmpDirIfNecessary(); err != nil {
		return fmt.Errorf("error initializing tmpdir: %s", err)
	}

	c.ffxIsolateDir = filepath.Join(c.TmpDir, "ffx-isolate")
	if fileExists(c.ffxIsolateDir) {
		// Already set up previously
		return nil
	}

	// If we fail after this point, remove any partially-constructed isolate
	// directory.
	defer func() {
		if returnErr != nil {
			// This will kill the daemon once it sees its socket deleted
			os.RemoveAll(c.ffxIsolateDir)
		}
	}()

	if err := os.Mkdir(c.ffxIsolateDir, os.ModeDir|0755); err != nil {
		return fmt.Errorf("error making isolate dir: %s", err)
	}

	// Add the target to the daemon (auto-starting it)
	// Note: The SSH private key config needs to be provided on the first call
	// to a daemon.
	addr := net.JoinHostPort(c.Host, strconv.Itoa(c.Port))
	if _, err := c.FfxRun("", "-c", "ssh.priv="+c.Key, "target", "add", addr); err != nil {
		return fmt.Errorf("error adding target to ffx: %s", err)
	}

	// Enable fuzz subcommand
	if _, err := c.FfxRun("", "config", "set", "fuzzing", "true"); err != nil {
		return fmt.Errorf("error enabling ffx fuzz: %s", err)
	}

	// Make sure the connection to fuzz manager is working
	if _, err := c.FfxRun("", "fuzz", "list"); err != nil {
		return fmt.Errorf("error testing ffx fuzz: %s", err)
	}

	return nil
}

// This is called from Connect, but split out separately here so that it can
// also be initialized in cases where we need to use the tempdir but don't
// actually need to connect to SSH/SFTP.
// TODO(fxbug.dev/106110): This logic can be cleaner once we drop v1
// support and do an initial ffx connection on Instance start.
func (c *SSHConnector) initializeTmpDirIfNecessary() error {
	if c.TmpDir != "" {
		return nil
	}

	// Initialize tempdir. This will be deleted when the Instance is cleaned up.
	tmpDir, err := os.MkdirTemp("", "undercoat-ffx-cache-")
	if err != nil {
		return fmt.Errorf("Error creating tempdir: %s", err)
	}
	c.TmpDir = tmpDir

	return nil
}

// Connect to the remote server
func (c *SSHConnector) Connect() (returnErr error) {
	if c.client != nil {
		return fmt.Errorf("Connect called, but already connected")
	}

	if err := c.initializeTmpDirIfNecessary(); err != nil {
		return fmt.Errorf("error initializing tmpdir: %s", err)
	}

	glog.Info("SSH: connecting...")
	key, err := os.ReadFile(c.Key)
	if err != nil {
		return fmt.Errorf("error reading ssh key: %s", err)
	}

	signer, err := ssh.ParsePrivateKey(key)
	if err != nil {
		return fmt.Errorf("error parsing ssh key: %s", err)
	}

	config := &ssh.ClientConfig{
		User: "clusterfuchsia",
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(signer),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}

	address := net.JoinHostPort(c.Host, strconv.Itoa(c.Port))

	var client *ssh.Client
	first := true
	for j := 1; j <= sshReconnectCount; j++ {
		if !first {
			glog.Warningf("Retrying in %s...", c.reconnectInterval)
			time.Sleep(c.reconnectInterval)
		}
		first = false

		// TODO(fxbug.dev/45424): dial timeout
		if client, err = ssh.Dial("tcp", address, config); err != nil {
			glog.Warningf("Got error during attempt %d: %s", j, err)
			continue
		}

		glog.Info("SSH: connected")
		c.client = client

		sftpClient, err := sftp.NewClient(c.client)
		if err != nil {
			return fmt.Errorf("error connecting sftp: %s", err)
		}

		glog.Info("SFTP: connected")
		c.sftpClient = sftpClient

		return nil
	}

	return fmt.Errorf("error connecting ssh")
}

// Close any open connections
func (c *SSHConnector) Close() {
	glog.Info("Closing SSH/SFTP")

	// TODO(fxbug.dev/47316): Look into errors thrown by these Closes when
	// disconnecting from in-memory SSH server
	if c.client != nil {
		if err := c.client.Close(); err != nil {
			glog.Warningf("Error while closing SSH: %s", err)
		}
		c.client = nil
	}

	if c.sftpClient != nil {
		if err := c.sftpClient.Close(); err != nil {
			glog.Warningf("Error while closing SFTP: %s", err)
		}
		c.sftpClient = nil
	}
}

// Command returns an InstanceCmd that can be used to given command over SSH
func (c *SSHConnector) Command(name string, args ...string) InstanceCmd {
	// TODO(fxbug.dev/45424): Would be best to shell escape
	cmdline := strings.Join(append([]string{name}, args...), " ")
	return &SSHInstanceCmd{connector: c, cmdline: cmdline}
}

// GetSysLog will fetch the syslog by running a remote command
func (c *SSHConnector) GetSysLog(pid int) (string, error) {
	cmd := c.Command("log_listener", "--dump_logs", "--pid", strconv.Itoa(pid))

	out, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return string(out), nil
}

// If the specified directory in the local cache is marked as mapping to a
// fuzzer's live corpus, fetch the corpus into place and remove the marker.
func (c *SSHConnector) fetchLiveCorpusIfNecessary(cacheDir string) error {
	marker := filepath.Join(cacheDir, liveCorpusMarkerName)
	if !fileExists(marker) {
		return nil
	}

	fuzzerUrl, err := os.ReadFile(marker)
	if err != nil {
		return fmt.Errorf("error reading marker file: %s", err)
	}

	tmpDir, err := os.MkdirTemp("", "undercoat-ffx-output-")
	if err != nil {
		return fmt.Errorf("error creating tempdir: %s", err)
	}
	defer os.RemoveAll(tmpDir)

	if _, err := c.FfxRun(tmpDir, "fuzz", "fetch", string(fuzzerUrl)); err != nil {
		return fmt.Errorf("error fetching live corpus: %s", err)
	}

	// Move all the files into the cache
	corpusOutputDir := filepath.Join(tmpDir, "corpus")
	if err := moveAllFiles(corpusOutputDir, cacheDir); err != nil {
		return fmt.Errorf("error moving live corpus into place: %s", err)
	}

	return os.Remove(marker)
}

// Calls fetchLiveCorpusIfNecessary for all directories appropriate for a given
// glob path within the local cache.
func (c *SSHConnector) fetchLiveCorporaIfNecessary(globPath string) error {
	srcList, err := filepath.Glob(globPath)
	if err != nil {
		return fmt.Errorf("error during glob expansion: %s", err)
	}

	// Special case for glob of an output corpus directory at top-level.
	if strings.HasSuffix(globPath, "/*") {
		srcList = append(srcList, filepath.Dir(globPath))
	}

	for _, root := range srcList {
		walker := fs.Walk(root)
		for walker.Step() {
			if err := walker.Err(); err != nil {
				return fmt.Errorf("error while walking %q: %s", root, err)
			}

			if !walker.Stat().IsDir() {
				continue
			}

			if err := c.fetchLiveCorpusIfNecessary(walker.Path()); err != nil {
				return fmt.Errorf("error fetching live corpus for %q: %s",
					walker.Path(), err)
			}
		}
	}

	return nil
}

// Get fetches files over SFTP, or from the local cache/remote corpus
func (c *SSHConnector) Get(targetSrc string, hostDst string) error {
	var targetFs fsInterface
	if isCachePath(targetSrc) {
		if err := c.initializeTmpDirIfNecessary(); err != nil {
			return fmt.Errorf("error initializing tmpdir: %s", err)
		}

		hostPath, err := c.cacheToHostPath(targetSrc)
		if err != nil {
			return err
		}
		targetSrc = hostPath
		targetFs = localFs{}

		// Look for any (sub)directories that are mapped to a live corpus, and
		// fetch them now so that the files will be picked up by Glob later.
		if err := c.fetchLiveCorporaIfNecessary(targetSrc); err != nil {
			return fmt.Errorf("error fetching live corpora: %s", err)
		}
	} else {
		if c.sftpClient == nil {
			if err := c.Connect(); err != nil {
				return err
			}
		}

		targetFs = sftpFs{c.sftpClient}
	}

	srcList, err := targetFs.Glob(targetSrc)
	if err != nil {
		return fmt.Errorf("error during glob expansion: %s", err)
	}
	if len(srcList) == 0 {
		return fmt.Errorf("no files matching glob: '%s'", targetSrc)
	}

	for _, root := range srcList {
		walker := targetFs.Walk(root)
		for walker.Step() {
			if err := walker.Err(); err != nil {
				return fmt.Errorf("error while walking %q: %s", root, err)
			}

			src := walker.Path()
			relPath, err := filepath.Rel(filepath.Dir(root), src)
			if err != nil {
				return fmt.Errorf("error taking relpath for %q: %s", src, err)
			}
			dst := path.Join(hostDst, relPath)

			// Create local directory if necessary
			if walker.Stat().IsDir() {
				if _, err := os.Stat(dst); os.IsNotExist(err) {
					os.Mkdir(dst, os.ModeDir|0755)
				}
				continue
			}

			if filepath.Base(src) == liveCorpusMarkerName {
				glog.Infof("Skipping live corpus marker: %s", src)
				continue
			}

			glog.Infof("Copying [remote]:%s to %s", src, dst)

			fin, err := targetFs.Open(src)
			if err != nil {
				return fmt.Errorf("error opening remote file: %s", err)
			}

			fout, err := os.Create(dst)
			if err != nil {
				fin.Close()
				return fmt.Errorf("error creating local file: %s", err)
			}

			_, err = io.Copy(fout, fin)

			// Close() immediately to free up resources since we're in a
			// potentially very large loop.
			fout.Close()
			fin.Close()

			if err != nil {
				return fmt.Errorf("error copying file: %s", err)
			}
		}
	}
	return nil
}

// Put uploads files over SFTP, or caches locally until needed
func (c *SSHConnector) Put(hostSrc string, targetDst string) error {
	var targetFs fsInterface
	if isCachePath(targetDst) {
		if err := c.initializeTmpDirIfNecessary(); err != nil {
			return fmt.Errorf("error initializing tmpdir: %s", err)
		}

		hostPath, err := c.cacheToHostPath(targetDst)
		if err != nil {
			return err
		}
		targetDst = hostPath
		targetFs = localFs{}
	} else {
		if c.sftpClient == nil {
			if err := c.Connect(); err != nil {
				return err
			}
		}

		targetFs = sftpFs{c.sftpClient}
	}

	// Expand any globs in source path
	srcList, err := filepath.Glob(hostSrc)
	if err != nil {
		return fmt.Errorf("error during glob expansion: %s", err)
	}
	if len(srcList) == 0 {
		return fmt.Errorf("no files matching glob: '%s'", hostSrc)
	}

	for _, root := range srcList {
		walker := fs.Walk(root)
		for walker.Step() {
			if err := walker.Err(); err != nil {
				return fmt.Errorf("error while walking %q: %s", root, err)
			}

			src := walker.Path()
			relPath, err := filepath.Rel(filepath.Dir(root), src)
			if err != nil {
				return fmt.Errorf("error taking relpath for %q: %s", src, err)
			}
			// filepath.Rel converts to host OS separators, while remote is always /
			dst := path.Join(targetDst, filepath.ToSlash(relPath))

			// Create remote subdirectory if necessary
			if walker.Stat().IsDir() {
				if _, err := targetFs.Stat(dst); err == nil {
					continue
				} else if !os.IsNotExist(err) {
					return fmt.Errorf("error stat-ing remote directory %q: %s", dst, err)
				}

				if err := targetFs.MkdirAll(dst); err != nil {
					return fmt.Errorf("error creating remote directory %q: %s", dst, err)
				}
				continue
			}

			// Create containing directories for remote file if necessary
			if err := targetFs.MkdirAll(path.Dir(dst)); err != nil {
				return fmt.Errorf("error creating remote directory %q: %s", dst, err)
			}

			glog.Infof("Copying %s to [remote]:%s", src, dst)

			fin, err := os.Open(src)
			if err != nil {
				return fmt.Errorf("error opening local file: %s", err)
			}

			fout, err := targetFs.Create(dst)
			if err != nil {
				fin.Close()
				return fmt.Errorf("error creating remote file: %s", err)
			}

			_, err = io.Copy(fout, fin)

			// Close() immediately to free up resources since we're in a
			// potentially very large loop.
			fout.Close()
			fin.Close()

			if err != nil {
				return fmt.Errorf("error copying file: %s", err)
			}
		}

	}
	return nil
}

// Any command that will end up attaching to a fuzzer requires this.
func ffxCommandRequiresOutputDir(args []string) bool {
	return len(args) >= 2 && args[0] == "fuzz" &&
		args[1] != "list" && args[1] != "stop"
}

// Only necessary for listing fuzzers.
func ffxCommandRequiresTestsJson(args []string) bool {
	return len(args) >= 2 && args[0] == "fuzz" &&
		args[1] == "list"
}

// FfxRun runs the specified command to completion via ffx and returns its
// output. `outputDir` is optional.
func (c *SSHConnector) FfxRun(outputDir string, args ...string) (string, error) {
	if ffxCommandRequiresOutputDir(args) && outputDir == "" {
		// The caller is not interested in the output files, but ffx fuzz still
		// requires a directory, so we will create a tempdir and clean it up
		// afterwards.
		tmpDir, err := os.MkdirTemp("", "undercoat-ffx-output-")
		if err != nil {
			return "", fmt.Errorf("error creating tempdir: %s", err)
		}
		outputDir = tmpDir
		defer os.RemoveAll(tmpDir)
	}

	cmd, err := c.FfxCommand(outputDir, args...)
	if err != nil {
		return "", err
	}

	cmd.Stderr = nil // Let Output() buffer it

	output, err := cmd.Output()
	if err != nil {
		if cmderr, ok := err.(*exec.ExitError); ok {
			glog.Errorf("ffx stdout: %s", output)
			glog.Errorf("ffx stderr: %s", cmderr.Stderr)
		}

		return "", fmt.Errorf("error calling ffx: %s", err)
	}
	return string(output), nil
}

// FfxCommand returns an exec.Cmd object that can be used to run the specified
// command via ffx.
func (c *SSHConnector) FfxCommand(outputDir string, args ...string) (*exec.Cmd, error) {
	// TODO(fxbug.dev/106110): Once we only support v2 fuzzer builds, we could
	// safely connect earlier.
	if c.ffxPath == "" {
		if err := c.initializeFfx(); err != nil {
			return nil, fmt.Errorf("error connecting ffx: %s", err)
		}
	}

	// Set output directory for any artifacts, etc.
	if ffxCommandRequiresOutputDir(args) && !fileExists(outputDir) {
		return nil, fmt.Errorf("output directory required for command: %s", args)
	}

	// Automatically translate any target paths to host paths
	for j, arg := range args {
		if isCachePath(arg) {
			hostPath, err := c.cacheToHostPath(arg)
			if err != nil {
				return nil, fmt.Errorf("invalid cache path %s: %s", arg, err)
			}
			args[j] = hostPath
		}
	}

	// Set explicit path to fuzzer metadata file
	if ffxCommandRequiresTestsJson(args) {
		paths, err := c.build.Path("tests.json")
		if err != nil {
			return nil, fmt.Errorf("tests.json not found in build: %s", err)
		}
		args = append(args, "-j", paths[0])
	}

	addr := net.JoinHostPort(c.Host, strconv.Itoa(c.Port))
	args = append([]string{"--target", addr,
		"--isolate-dir", c.ffxIsolateDir}, args...)

	if outputDir != "" {
		args = append(args, "-o", outputDir)
	}

	cmd := NewCommand(c.ffxPath, args...)
	cmd.Stderr = os.Stderr // TODO: save to buffer instead

	return cmd, nil
}

// Cleans up any temporary files used by the Connector
func (c *SSHConnector) Cleanup() {
	c.Close()

	if c.TmpDir == "" {
		return
	}
	// Best-effort at cleanly shutting down the daemon. Even if this fails,
	// it should self-cleanup once it sees we've removed the isolate dir.
	ffxIsolateDir := filepath.Join(c.TmpDir, "ffx-isolate")
	if fileExists(ffxIsolateDir) {
		c.FfxRun("", "daemon", "stop")
	}

	if err := os.RemoveAll(c.TmpDir); err != nil {
		glog.Warningf("failed to remove temp dir: %s", err)
	}
	c.TmpDir = ""
}

// V2 target paths are given a special prefix to indicate that they refer to an
// emulated cache filesystem and not an entry on the actual target
const cachePrefix = "CACHE:"

func (c *SSHConnector) cacheToHostPath(targetPath string) (string, error) {
	if !isCachePath(targetPath) {
		return "", fmt.Errorf("invalid (non-cache) path: %s", targetPath)
	}

	if c.TmpDir == "" {
		return "", fmt.Errorf("tmpdir was not set")
	}

	path := strings.TrimPrefix(targetPath, cachePrefix)
	return filepath.Join(c.TmpDir, path), nil
}

func isCachePath(path string) bool {
	return strings.HasPrefix(path, cachePrefix)
}

// Recursively deletes the given directory on the target.
func (c *SSHConnector) RmDir(path string) error {
	if isCachePath(path) {
		path, err := c.cacheToHostPath(path)
		if err != nil {
			return fmt.Errorf("invalid cache path %s: %s", path, err)
		}
		return os.RemoveAll(path)
	}

	return c.Command("rm", "-rf", path).Run()
}

// Returns whether the target path is a directory or not.
// Note: Only supported/necessary for V2 fuzzers.
func (c *SSHConnector) IsDir(path string) (bool, error) {
	localPath, err := c.cacheToHostPath(path)
	if err != nil {
		return false, fmt.Errorf("invalid cache path %s: %s", path, err)
	}
	fileInfo, err := os.Stat(localPath)
	if err != nil {
		return false, fmt.Errorf("invalid cached input %q: %s", localPath, err)
	}
	return fileInfo.IsDir(), nil
}

func loadConnectorFromHandle(build Build, handle Handle, verify bool) (Connector, error) {
	handleData, err := handle.GetData(verify)
	if err != nil {
		return nil, err
	}

	// Check that the Connector is in a valid state
	switch conn := handleData.connector.(type) {
	case *SSHConnector:
		if verify {
			if conn.Host == "" {
				return nil, fmt.Errorf("host not found in handle")
			}
			if conn.Port == 0 {
				return nil, fmt.Errorf("port not found in handle")
			}
			if conn.Key == "" {
				return nil, fmt.Errorf("key not found in handle")
			}
			if conn.TmpDir == "" {
				return nil, fmt.Errorf("tmpDir not found in handle")
			}
		}
		conn.build = build
		return conn, nil
	default:
		if verify {
			return nil, fmt.Errorf("unknown connector type: %T", handleData.connector)
		} else {
			return nil, nil
		}
	}
}

// Generate a key to use for SSH
func createSSHKey() (*rsa.PrivateKey, error) {
	privKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return nil, fmt.Errorf("error generating keypair: %s", err)
	}

	return privKey, nil
}

// Writes private key to given path in format usable by SSH
func writeSSHPrivateKeyFile(key *rsa.PrivateKey, path string) error {
	pemData := pem.EncodeToMemory(&pem.Block{
		Type:  "RSA PRIVATE KEY",
		Bytes: x509.MarshalPKCS1PrivateKey(key),
	})

	if err := os.WriteFile(path, pemData, 0o600); err != nil {
		return fmt.Errorf("error writing private key file: %s", err)
	}

	return nil
}

// Writes public key to given path in format usable by SSH
func writeSSHPublicKeyFile(key *rsa.PrivateKey, path string) error {
	pubKey, err := ssh.NewPublicKey(key.Public())
	if err != nil {
		return fmt.Errorf("error generating public key: %s", err)
	}
	pubKeyData := ssh.MarshalAuthorizedKey(pubKey)
	if err := os.WriteFile(path, pubKeyData, 0o644); err != nil {
		return fmt.Errorf("error writing public key: %s", err)
	}

	return nil
}
