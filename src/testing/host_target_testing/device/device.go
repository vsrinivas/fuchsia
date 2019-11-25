// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"os"
	"strings"
	"sync"
	"time"

	"fuchsia.googlesource.com/host_target_testing/packages"
	"fuchsia.googlesource.com/host_target_testing/sl4f"
	"fuchsia.googlesource.com/host_target_testing/sshclient"

	"golang.org/x/crypto/ssh"
)

// Client manages the connection to the device.
type Client struct {
	deviceHostname string
	sshClient      *sshclient.Client
}

// NewClient creates a new Client.
func NewClient(deviceHostname string, privateKey ssh.Signer) (*Client, error) {
	sshConfig, err := newSSHConfig(privateKey)
	if err != nil {
		return nil, err
	}
	sshClient := sshclient.NewClient(net.JoinHostPort(deviceHostname, "22"), sshConfig)

	return &Client{
		deviceHostname: deviceHostname,
		sshClient:      sshClient,
	}, nil
}

// Construct a new `ssh.ClientConfig` for a given key file, or return an error if
// the key is invalid.
func newSSHConfig(privateKey ssh.Signer) (*ssh.ClientConfig, error) {
	config := &ssh.ClientConfig{
		User: "fuchsia",
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(privateKey),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
		Timeout:         30 * time.Second,
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

// WaitForDeviceToBeConnected blocks until a device is available for access.
func (c *Client) WaitForDeviceToBeConnected() {
	c.sshClient.WaitToBeConnected()
}

// RegisterDisconnectListener adds a waiter that gets notified when the ssh and
// shell is disconnected.
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

// RebootToRecovery asks the device to reboot into the recovery partition. It
// waits until the device disconnects before returning.
func (c *Client) RebootToRecovery() error {
	log.Printf("rebooting to recovery")

	var wg sync.WaitGroup
	c.RegisterDisconnectListener(&wg)

	err := c.Run("dm reboot-recovery", os.Stdout, os.Stderr)
	if err != nil {
		if _, ok := err.(*ssh.ExitMissingError); !ok {
			return fmt.Errorf("failed to reboot into recovery: %s", err)
		}
	}

	// Wait until we get a signal that we have disconnected
	wg.Wait()

	return nil
}

// TriggerSystemOTA gets the device to perform a system update, ensuring it
// reboots as expected. rpcClient, if provided, will be used and re-connected
func (c *Client) TriggerSystemOTA(repo *packages.Repository, rpcClient **sl4f.Client) error {
	log.Printf("triggering OTA")

	rebootCheckPath := "/tmp/ota_test_should_reboot"
	if *rpcClient != nil {
		// Write a file to /tmp that should be lost after a reboot, to
		// ensure the device actually reboots instead of just
		// disconnects from the network for a bit.
		if err := (*rpcClient).FileWrite(rebootCheckPath, []byte("yes")); err != nil {
			return fmt.Errorf("failed to write reboot check file: %s", err)
		}
		stat, err := (*rpcClient).PathStat(rebootCheckPath)
		if err != nil {
			return fmt.Errorf("failed to stat reboot check file: %s", err)
		}
		if expected := (sl4f.PathMetadata{Mode: 0, Size: 3}); stat != expected {
			return fmt.Errorf("unexpected reboot check file metadata: expected %v, got %v", expected, stat)
		}
	}

	var wg sync.WaitGroup
	c.RegisterDisconnectListener(&wg)

	if err := c.Run("amberctl system_update", os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("failed to trigger OTA: %s", err)
	}

	// Wait until we get a signal that we have disconnected
	wg.Wait()

	c.WaitForDeviceToBeConnected()

	if *rpcClient != nil {
		// FIXME: It would make sense to be able to close the rpcClient
		// before the reboot, but for an unknown reason, closing this
		// session will cause the entire ssh connection to disconnect
		// and reconnect, causing the test to assume the device
		// rebooted and start verifying that the OTA succeeded, when,
		// in reality, it likely hasn't finished yet.
		(*rpcClient).Close()
		*rpcClient = nil

		var err error
		*rpcClient, err = c.StartRpcSession(repo)
		if err != nil {
			// FIXME(40913): every builder should at least build
			// sl4f as a universe package.
			log.Printf("unable to connect to sl4f after OTA: %s", err)
			//return fmt.Errorf("unable to connect to sl4f after OTA: %s", err)
		}

		// Make sure the device actually rebooted by verifying the
		// reboot check file no longer exists.
		var exists bool
		if *rpcClient == nil {
			exists, err = c.RemoteFileExists(rebootCheckPath)
		} else {
			exists, err = (*rpcClient).PathExists(rebootCheckPath)
		}

		if err != nil {
			return fmt.Errorf("failed to stat reboot check file: %s", err)
		} else if exists {
			return errors.New("reboot check file exists after an OTA, device did not reboot")
		}
	}

	log.Printf("device rebooted")

	return nil
}

// ValidateStaticPackages checks that all static packages have no missing blobs.
func (c *Client) ValidateStaticPackages() error {
	log.Printf("validating static packages")

	path := "/pkgfs/ctl/validation/missing"
	f, err := c.ReadRemotePath(path)
	if err != nil {
		return fmt.Errorf("error reading %q: %s", path, err)
	}

	merkles := strings.TrimSpace(string(f))
	if merkles != "" {
		return fmt.Errorf("static packages are missing the following blobs:\n%s", merkles)
	}

	log.Printf("all static package blobs are accounted for")
	return nil
}

// ReadRemotePath read a file off the remote device.
func (c *Client) ReadRemotePath(path string) ([]byte, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := c.sshClient.Run(fmt.Sprintf(
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

// DeleteRemotePath deletes a file off the remote device.
func (c *Client) DeleteRemotePath(path string) error {
	var stderr bytes.Buffer
	err := c.sshClient.Run(fmt.Sprintf("PATH= rm %q", path), os.Stdout, &stderr)
	if err != nil {
		return fmt.Errorf("failed to delete %q: %s: %s", path, err, string(stderr.Bytes()))
	}

	return nil
}

// RemoteFileExists checks if a file exists on the remote device.
func (c *Client) RemoteFileExists(path string) (bool, error) {
	var stderr bytes.Buffer
	err := c.Run(fmt.Sprintf("PATH= ls %s", path), ioutil.Discard, &stderr)
	if err == nil {
		return true, nil
	}

	if e, ok := err.(*ssh.ExitError); ok {
		if e.ExitStatus() == 1 {
			return false, nil
		}
	}

	return false, fmt.Errorf("error reading %q: %s: %s", path, err, string(stderr.Bytes()))
}

// RegisterPackageRepository adds the repository as a repository inside the device.
func (c *Client) RegisterPackageRepository(repo *packages.Server) error {
	log.Printf("registering package repository: %s", repo.Dir)
	cmd := fmt.Sprintf("amberctl add_src -f %s -h %s", repo.URL, repo.Hash)
	return c.Run(cmd, os.Stdout, os.Stderr)
}

func (c *Client) StartRpcSession(repo *packages.Repository) (*sl4f.Client, error) {
	// Determine the address of this device from the point of view of the target.
	localHostname, err := c.sshClient.GetSshConnection()
	if err != nil {
		return nil, err
	}

	// Ensure this client is running system_image or system_image_prime from repo.
	currentSystemImageMerkle, err := c.GetSystemImageMerkle()
	if err != nil {
		return nil, err
	}
	if err := repo.VerifyMatchesAnyUpdateSystemImageMerkle(currentSystemImageMerkle); err != nil {
		return nil, err
	}

	// Serve the package repository.
	repoName := "host_target_testing_sl4f"
	repoServer, err := repo.Serve(localHostname, repoName)
	if err != nil {
		return nil, err
	}
	defer repoServer.Shutdown(context.Background())

	// Configure the target to use this repository as "fuchsia-pkg://host_target_testing".
	log.Printf("registering package repository: %s", repoServer.Dir)
	cmd := fmt.Sprintf("amberctl add_repo_cfg -f %s -h %s", repoServer.URL, repoServer.Hash)
	if err := c.sshClient.Run(cmd, os.Stdout, os.Stderr); err != nil {
		return nil, err
	}

	rpcClient, err := sl4f.NewClient(c.sshClient, net.JoinHostPort(c.deviceHostname, "80"), repoName)
	if err != nil {
		return nil, fmt.Errorf("error creating sl4f client: %s", err)
	}

	return rpcClient, nil
}
