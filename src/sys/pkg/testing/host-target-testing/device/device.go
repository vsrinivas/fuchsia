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

const rebootCheckPath = "/tmp/ota_test_should_reboot"

// Client manages the connection to the device.
type Client struct {
	deviceHostname string
	addr           string
	sshConfig      *ssh.ClientConfig

	// This mutex protects the following fields.
	mu        sync.Mutex
	sshClient *sshclient.Client
}

// NewClient creates a new Client.
func NewClient(ctx context.Context, deviceHostname string, privateKey ssh.Signer) (*Client, error) {
	sshConfig, err := newSSHConfig(privateKey)
	if err != nil {
		return nil, err
	}

	addr := net.JoinHostPort(deviceHostname, "22")
	sshClient, err := sshclient.NewClient(ctx, addr, sshConfig)
	if err != nil {
		return nil, err
	}

	return &Client{
		deviceHostname: deviceHostname,
		addr:           addr,
		sshConfig:      sshConfig,
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
	c.mu.Lock()
	defer c.mu.Unlock()

	c.sshClient.Close()
}

func (c *Client) Reconnect(ctx context.Context) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.sshClient.Close()

	sshClient, err := sshclient.NewClient(ctx, c.addr, c.sshConfig)
	if err != nil {
		return err
	}
	c.sshClient = sshClient

	return nil
}

// Run a command to completion on the remote device and write STDOUT and STDERR
// to the passed in io.Writers.
func (c *Client) Run(ctx context.Context, command string, stdout io.Writer, stderr io.Writer) error {
	c.mu.Lock()
	sshClient := c.sshClient
	c.mu.Unlock()

	return sshClient.Run(ctx, command, stdout, stderr)
}

// RegisterDisconnectListener adds a waiter that gets notified when the ssh and
// shell is disconnected.
func (c *Client) RegisterDisconnectListener(ch chan struct{}) {
	c.mu.Lock()
	sshClient := c.sshClient
	c.mu.Unlock()

	sshClient.RegisterDisconnectListener(ch)
}

func (c *Client) GetSshConnection(ctx context.Context) (string, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := c.Run(ctx, "PATH= echo $SSH_CONNECTION", &stdout, &stderr)
	if err != nil {
		return "", fmt.Errorf("failed to read SSH_CONNECTION: %s: %s", err, string(stderr.Bytes()))
	}
	return strings.Split(string(stdout.Bytes()), " ")[0], nil
}

func (c *Client) GetSystemImageMerkle(ctx context.Context) (string, error) {
	const systemImageMeta = "/system/meta"
	merkle, err := c.ReadRemotePath(ctx, systemImageMeta)
	if err != nil {
		return "", err
	}

	return strings.TrimSpace(string(merkle)), nil
}

// Reboot asks the device to reboot. It waits until the device to reconnect
// before returning.
func (c *Client) Reboot(ctx context.Context, repo *packages.Repository, rpcClient **sl4f.Client) error {
	log.Printf("rebooting")

	return c.ExpectReboot(ctx, repo, rpcClient, func() error {
		err := c.Run(ctx, "dm reboot", os.Stdout, os.Stderr)
		if err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); !ok {
				return fmt.Errorf("failed to reboot: %s", err)
			}
		}

		return nil
	})
}

// RebootToRecovery asks the device to reboot into the recovery partition. It
// waits until the device disconnects before returning.
func (c *Client) RebootToRecovery(ctx context.Context) error {
	log.Printf("rebooting to recovery")

	return c.ExpectDisconnect(ctx, func() error {
		err := c.Run(ctx, "dm reboot-recovery", os.Stdout, os.Stderr)
		if err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); !ok {
				return fmt.Errorf("failed to reboot into recovery: %s", err)
			}
		}

		return nil
	})
}

func (c *Client) ReadBasePackages(ctx context.Context) (map[string]string, error) {
	b, err := c.ReadRemotePath(ctx, "/pkgfs/system/data/static_packages")
	if err != nil {
		return nil, err
	}

	pkgs := make(map[string]string)
	for _, line := range strings.Split(string(b), "\n") {
		if line == "" {
			break
		}
		parts := strings.SplitN(line, "=", 2)
		pkgs[parts[0]] = parts[1]
	}

	return pkgs, nil
}

// TriggerSystemOTA gets the device to perform a system update, ensuring it
// reboots as expected. rpcClient, if provided, will be used and re-connected
func (c *Client) TriggerSystemOTA(ctx context.Context, repo *packages.Repository, rpcClient **sl4f.Client) error {
	log.Printf("Triggering OTA")
	startTime := time.Now()

	basePackages, err := c.ReadBasePackages(ctx)
	if err != nil {
		return err
	}

	updateBinMerkle, ok := basePackages["update-bin/0"]
	if !ok {
		return fmt.Errorf("base packages doesn't include update-bin/0 package")
	}

	return c.ExpectReboot(ctx, repo, rpcClient, func() error {
		server, err := c.ServePackageRepository(ctx, repo, "trigger-ota", true)
		if err != nil {
			return fmt.Errorf("error setting up server: %s", err)
		}
		defer server.Shutdown(ctx)

		// FIXME: running this out of /pkgfs/versions is unsound WRT using the correct loader service
		// Adding this as a short-term hack to unblock http://fxb/47213
		cmd := fmt.Sprintf("/pkgfs/versions/%s/bin/update check-now --monitor", updateBinMerkle)
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); !ok {
				return fmt.Errorf("failed to trigger OTA: %s", err)
			}
		}

		return nil
	})
	if err != nil {
		return err
	}

	log.Printf("OTA completed in %s", time.Now().Sub(startTime))

	return nil
}

func (c *Client) ExpectDisconnect(ctx context.Context, f func() error) error {
	ch := make(chan struct{})
	c.RegisterDisconnectListener(ch)

	if err := f(); err != nil {
		// It's okay if we leak the disconnect listener, it'll get
		// cleaned up next time the device disconnects.
		return err
	}

	// Wait until we get a signal that we have disconnected
	select {
	case <-ch:
	case <-ctx.Done():
		return fmt.Errorf("device did not disconnect: %s", ctx.Err())
	}

	log.Printf("device disconnected")

	return nil
}

func (c *Client) ExpectReboot(ctx context.Context, repo *packages.Repository, rpcClient **sl4f.Client, f func() error) error {
	if err := c.setupReboot(ctx, rpcClient); err != nil {
		return err
	}

	ch := make(chan struct{})
	c.RegisterDisconnectListener(ch)

	if err := f(); err != nil {
		// It's okay if we leak the disconnect listener, it'll get
		// cleaned up next time the device disconnects.
		return err
	}

	// Wait until we get a signal that we have disconnected
	select {
	case <-ch:
	case <-ctx.Done():
		return fmt.Errorf("device did not disconnect: %s", ctx.Err())
	}

	log.Printf("device disconnected, waiting for device to boot")

	if err := c.Reconnect(ctx); err != nil {
		return fmt.Errorf("failed to reconnect: %s", err)
	}

	if err := c.verifyReboot(ctx, repo, rpcClient); err != nil {
		return fmt.Errorf("failed to verify reboot: %s", err)
	}

	log.Printf("device rebooted")

	return nil
}

func (c *Client) setupReboot(ctx context.Context, rpcClient **sl4f.Client) error {
	if *rpcClient != nil {
		// Write a file to /tmp that should be lost after a reboot, to
		// ensure the device actually reboots instead of just
		// disconnects from the network for a bit.
		if err := (*rpcClient).FileWrite(ctx, rebootCheckPath, []byte("yes")); err != nil {
			return fmt.Errorf("failed to write reboot check file: %s", err)
		}
		stat, err := (*rpcClient).PathStat(ctx, rebootCheckPath)
		if err != nil {
			return fmt.Errorf("failed to stat reboot check file: %s", err)
		}
		if expected := (sl4f.PathMetadata{Mode: 0, Size: 3}); stat != expected {
			return fmt.Errorf("unexpected reboot check file metadata: expected %v, got %v", expected, stat)
		}
	}

	return nil
}

func (c *Client) verifyReboot(ctx context.Context, repo *packages.Repository, rpcClient **sl4f.Client) error {
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
		*rpcClient, err = c.StartRpcSession(ctx, repo)
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
			exists, err = c.RemoteFileExists(ctx, rebootCheckPath)
		} else {
			exists, err = (*rpcClient).PathExists(ctx, rebootCheckPath)
		}

		if err != nil {
			return fmt.Errorf("failed to stat reboot check file: %s", err)
		} else if exists {
			return errors.New("reboot check file exists after an OTA, device did not reboot")
		}
	}

	return nil
}

// ValidateStaticPackages checks that all static packages have no missing blobs.
func (c *Client) ValidateStaticPackages(ctx context.Context) error {
	log.Printf("validating static packages")

	path := "/pkgfs/ctl/validation/missing"
	f, err := c.ReadRemotePath(ctx, path)
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
func (c *Client) ReadRemotePath(ctx context.Context, path string) ([]byte, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := c.Run(ctx, fmt.Sprintf(
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
func (c *Client) DeleteRemotePath(ctx context.Context, path string) error {
	var stderr bytes.Buffer
	err := c.Run(ctx, fmt.Sprintf("PATH= rm %q", path), os.Stdout, &stderr)
	if err != nil {
		return fmt.Errorf("failed to delete %q: %s: %s", path, err, string(stderr.Bytes()))
	}

	return nil
}

// RemoteFileExists checks if a file exists on the remote device.
func (c *Client) RemoteFileExists(ctx context.Context, path string) (bool, error) {
	var stderr bytes.Buffer
	err := c.Run(ctx, fmt.Sprintf("PATH= ls %s", path), ioutil.Discard, &stderr)
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
func (c *Client) RegisterPackageRepository(ctx context.Context, repo *packages.Server, createRewriteRule bool) error {
	log.Printf("registering package repository: %s", repo.Dir)
	var cmd string
	if createRewriteRule {
		cmd = fmt.Sprintf("amberctl add_src -f %s -h %s -verbose", repo.URL, repo.Hash)
	} else {
		cmd = fmt.Sprintf("amberctl add_repo_cfg -f %s -h %s -verbose", repo.URL, repo.Hash)
	}
	return c.Run(ctx, cmd, os.Stdout, os.Stderr)
}

func (c *Client) ServePackageRepository(
	ctx context.Context,
	repo *packages.Repository,
	name string,
	createRewriteRule bool,
) (*packages.Server, error) {
	// Make sure the device doesn't have any broken static packages.
	if err := c.ValidateStaticPackages(ctx); err != nil {
		return nil, err
	}

	// Tell the device to connect to our repository.
	localHostname, err := c.GetSshConnection(ctx)
	if err != nil {
		return nil, err
	}

	// Serve the repository before the test begins.
	server, err := repo.Serve(ctx, localHostname, name)
	if err != nil {
		return nil, err
	}

	if err := c.RegisterPackageRepository(ctx, server, createRewriteRule); err != nil {
		server.Shutdown(ctx)
		return nil, err
	}

	return server, nil
}

func (c *Client) StartRpcSession(ctx context.Context, repo *packages.Repository) (*sl4f.Client, error) {
	log.Printf("connecting to sl4f")
	startTime := time.Now()

	c.mu.Lock()
	sshClient := c.sshClient
	c.mu.Unlock()

	// Ensure this client is running system_image or system_image_prime from repo.
	currentSystemImageMerkle, err := c.GetSystemImageMerkle(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get system image merkle: %s", err)
	}
	if err := repo.VerifyMatchesAnyUpdateSystemImageMerkle(currentSystemImageMerkle); err != nil {
		return nil, fmt.Errorf("repo does not match system version: %s", err)
	}

	// Configure the target to use this repository as "fuchsia-pkg://host_target_testing_sl4f".
	repoName := "host_target_testing_sl4f"
	repoServer, err := c.ServePackageRepository(ctx, repo, repoName, false)
	if err != nil {
		return nil, fmt.Errorf("error serving repo to device: %s", err)
	}
	defer repoServer.Shutdown(ctx)

	rpcClient, err := sl4f.NewClient(ctx, sshClient, net.JoinHostPort(c.deviceHostname, "80"), repoName)
	if err != nil {
		return nil, fmt.Errorf("error creating sl4f client: %s", err)
	}

	log.Printf("connected to sl4f in %s", time.Now().Sub(startTime))

	return rpcClient, nil
}

func (c *Client) DownloadOTA(ctx context.Context, repo *packages.Repository, updatePackageUrl string) error {
	log.Printf("Downloading OTA")
	startTime := time.Now()

	server, err := c.ServePackageRepository(ctx, repo, "download-ota", true)
	if err != nil {
		return fmt.Errorf("error setting up server: %s", err)
	}
	defer server.Shutdown(ctx)

	// In order to manually trigger the system updater, we need the `run`
	// package. Since builds can be configured to not automatically install
	// packages, we need to explicitly resolve it.
	err = c.Run(ctx, "pkgctl resolve fuchsia-pkg://fuchsia.com/run/0", os.Stdout, os.Stderr)
	if err != nil {
		return fmt.Errorf("error resolving the run package: %v", err)
	}

	log.Printf("Downloading system OTA")

	cmd := fmt.Sprintf("run \"fuchsia-pkg://fuchsia.com/amber#meta/system_updater.cmx\" --initiator manual --reboot=false --update \"%s\"", updatePackageUrl)
	if err = c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
		return fmt.Errorf("failed to run system_updater.cmx: %s", err)
	}

	log.Printf("OTA successfully downloaded in %s", time.Now().Sub(startTime))

	return nil
}
