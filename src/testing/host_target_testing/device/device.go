// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"os"
	"os/exec"
	"strings"
	"sync"
	"testing"
	"time"

	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/sshclient"

	"golang.org/x/crypto/ssh"
)

// Client manages the connection to the device.
type Client struct {
	deviceHostname string
	sshClient      *sshclient.Client
}

// NewClient creates a new Client.
func NewClient(deviceHostname, keyFile string) (*Client, error) {
	sshConfig, err := newSSHConfig(keyFile)
	if err != nil {
		return nil, err
	}
	sshClient, err := sshclient.NewClient(net.JoinHostPort(deviceHostname, "22"), sshConfig)
	if err != nil {
		return nil, err
	}

	return &Client{
		deviceHostname: deviceHostname,
		sshClient:      sshClient,
	}, nil
}

// Construct a new `ssh.ClientConfig` for a given key file, or return an error if
// the key is invalid.
func newSSHConfig(keyFile string) (*ssh.ClientConfig, error) {
	key, err := ioutil.ReadFile(keyFile)
	if err != nil {
		return nil, err
	}

	privateKey, err := ssh.ParsePrivateKey(key)
	if err != nil {
		return nil, err
	}

	config := &ssh.ClientConfig{
		User: "fuchsia",
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(privateKey),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
		Timeout:         1 * time.Second,
	}

	return config, nil
}

// Close the Client connection
func (c *Client) Close() {
	c.sshClient.Close()
}

// Run a command to completion on the remote device and write STDOUT and STDERR
// to the passed in io.Writers.
func (c *Client) Run(command string, stdout io.Writer, stderr io.Writer) error {
	return c.sshClient.Run(command, stdout, stderr)
}

// WaitForDeviceToBeUp blocks until a device is available for access.
func (c *Client) WaitForDeviceToBeUp(t *testing.T) {
	log.Printf("waiting for device %q to ping", c.deviceHostname)
	path, err := exec.LookPath("/bin/ping")
	if err != nil {
		t.Fatal(err)
	}

	for {
		out, err := exec.Command(path, "-c", "1", "-W", "1", c.deviceHostname).Output()
		if err == nil {
			break
		}
		log.Printf("%s - %s", err, out)

		time.Sleep(1 * time.Second)
	}

	log.Printf("wait for ssh to be connect")
	for !c.sshClient.IsConnected() {
		time.Sleep(1 * time.Second)
	}

	log.Printf("device up")
}

// RegisterDisconnectListener adds a waiter that gets notified when the ssh
// client is disconnected.
func (c *Client) RegisterDisconnectListener(wg *sync.WaitGroup) {
	c.sshClient.RegisterDisconnectListener(wg)
}

func (c *Client) GetSshConnection() (string, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := c.Run("PATH= echo $SSH_CONNECTION", &stdout, &stderr)
	if err != nil {
		return "", fmt.Errorf("failed to read SSH_CONNECTION: %s: %s", err, string(stderr.Bytes()))
	}
	return strings.Split(string(stdout.Bytes()), " ")[0], nil
}

func (c *Client) GetSystemImageMerkle() (string, error) {
	const systemImageMeta = "/system/meta"
	merkle, err := c.ReadRemotePath(systemImageMeta)
	if err != nil {
		return "", err
	}

	return strings.TrimSpace(string(merkle)), nil
}

// GetBuildSnapshot fetch the device's current system version, as expressed by the file
// /config/build-info/snapshot.
func (c *Client) GetBuildSnapshot(t *testing.T) []byte {
	const buildInfoSnapshot = "/config/build-info/snapshot"
	snapshot, err := c.ReadRemotePath(buildInfoSnapshot)
	if err != nil {
		t.Fatalf("failed to read %q: %s", buildInfoSnapshot, err)
	}

	return snapshot
}

// TriggerSystemOTA gets the device to perform a system update.
func (c *Client) TriggerSystemOTA(t *testing.T) {
	log.Printf("triggering OTA")

	var wg sync.WaitGroup
	c.RegisterDisconnectListener(&wg)

	if err := c.Run("amberctl system_update", os.Stdout, os.Stderr); err != nil {
		t.Fatalf("failed to trigger OTA: %s", err)
	}

	// Wait until we get a signal that we have disconnected
	wg.Wait()

	c.WaitForDeviceToBeUp(t)
	log.Printf("device rebooted")
}

// ValidateStaticPackages checks that all static packages have no missing blobs.
func (c *Client) ValidateStaticPackages(t *testing.T) {
	log.Printf("validating static packages")

	path := "/pkgfs/ctl/validation/missing"
	f, err := c.ReadRemotePath(path)
	if err != nil {
		t.Fatalf("error reading %q: %s", path, err)
	}

	merkles := strings.TrimSpace(string(f))
	if merkles != "" {
		t.Fatalf("static packages are missing the following blobs:\n%s", merkles)
	}

	log.Printf("all static package blobs are accounted for")
}

// ReadRemotePath read a file off the remote device.
func (c *Client) ReadRemotePath(path string) ([]byte, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := c.Run(fmt.Sprintf(
		`(
		test -e "%s" &&
		while IFS='' read f; do
			echo "$f";
		done < "%s" &&
		if [ ! -z "$f" ];
			then echo "$f";
		fi
		)`, path, path), &stdout, &stderr)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %s: %s", path, err, string(stderr.Bytes()))
	}

	return stdout.Bytes(), nil
}

// RemoteFileExists checks if a file exists on the remote device.
func (c *Client) RemoteFileExists(t *testing.T, path string) bool {
	var stderr bytes.Buffer
	err := c.Run(fmt.Sprintf("PATH= ls %s", path), ioutil.Discard, &stderr)
	if err == nil {
		return true
	}

	if e, ok := err.(*ssh.ExitError); ok {
		if e.ExitStatus() == 1 {
			return false
		}
	}

	t.Fatalf("error reading %q: %s: %s", path, err, string(stderr.Bytes()))

	return false
}

// RegisterPackageRepository adds the repository as a repository inside the device.
func (c *Client) RegisterPackageRepository(repo *packages.Server) error {
	log.Printf("registering package repository: %s", repo.Dir)
	cmd := fmt.Sprintf("amberctl add_src -f %s -h %s", repo.URL, repo.Hash)
	return c.Run(cmd, os.Stdout, os.Stderr)
}

// Wait for the path to exist on the device.
func (c *Client) waitForDevicePath(path string) {
	for {
		log.Printf("waiting for %q to mount", path)
		err := c.Run(fmt.Sprintf("PATH= ls %s", path), ioutil.Discard, ioutil.Discard)
		if err == nil {
			break
		}

		log.Printf("sleeping")
		time.Sleep(1 * time.Second)
	}
}
