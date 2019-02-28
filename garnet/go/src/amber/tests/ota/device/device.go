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
	"testing"
	"time"

	"system_ota/amber"
	"system_ota/sshclient"

	"golang.org/x/crypto/ssh"
)

const (
	remoteDevmgrPath = "/boot/config/devmgr"
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

// GetCurrentDevmgrConfig fetches the device's current system version, as
// expressed by the file /boot/config/devmgr.
func (c *Client) GetCurrentDevmgrConfig(t *testing.T) []byte {
	// Make sure that the device does not have the /boot/config/devmgr we
	// are OTA-ing to. In addition, make sure our package and system
	// package do not exist on the system.
	devmgr, err := c.ReadRemotePath(remoteDevmgrPath)
	if err != nil {
		t.Fatalf("failed to read %q", remoteDevmgrPath)
	}

	return devmgr
}

// TriggerSystemOTA gets the device to perform a system update.
func (c *Client) TriggerSystemOTA(t *testing.T) {
	log.Printf("triggering OTA")

	err := c.Run("amber_ctl system_update", os.Stdout, os.Stderr)
	if err != nil {
		t.Fatalf("failed to trigger OTA: %s", err)
	}

	// Wait until we get a signal that we have disconnected
	c.sshClient.WaitUntilDisconnected()
	log.Printf("disconnected from device")

	c.WaitForDeviceToBeUp(t)
	log.Printf("device rebooted")

	c.waitForDevicePath(t, "/boot")
	c.waitForDevicePath(t, "/system")
	c.waitForDevicePath(t, "/pkgfs")
}

// ReadRemotePath read a file off the remote device.
func (c *Client) ReadRemotePath(path string) ([]byte, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := c.Run(fmt.Sprintf("/bin/cat %s", path), &stdout, &stderr)
	if err != nil {
		return nil, fmt.Errorf("failed to read %q: %s: %s", path, err, string(stderr.Bytes()))
	}

	return stdout.Bytes(), nil
}

// RemoteFileExists checks if a file exists on the remote device.
func (c *Client) RemoteFileExists(t *testing.T, path string) bool {
	var stderr bytes.Buffer
	err := c.Run(fmt.Sprintf("/bin/ls %s", path), ioutil.Discard, &stderr)
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

// RegisterAmberSource adds the repository as a source inside the device's amber.
func (c *Client) RegisterAmberSource(repoDir string, localHostname string) error {
	log.Printf("registering devhost as update source")

	configURL, configHash, err := amber.WriteConfig(repoDir, localHostname)
	if err != nil {
		return err
	}

	cmd := fmt.Sprintf("amber_ctl add_src -f %s -h %s", configURL, configHash)
	err = c.Run(cmd, os.Stdout, os.Stderr)
	if err != nil {
		return err
	}
	return nil
}

// Wait for the path to exist on the device.
func (c *Client) waitForDevicePath(t *testing.T, path string) {
	for {
		log.Printf("waiting for %q to mount", path)
		err := c.Run(fmt.Sprintf("/bin/ls %s", path), ioutil.Discard, ioutil.Discard)
		if err == nil {
			break
		}

		log.Printf("sleeping")
		time.Sleep(1 * time.Second)
	}
}
