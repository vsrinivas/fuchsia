// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package device

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/artifacts"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/packages"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/paver"
	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/sl4f"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"golang.org/x/crypto/ssh"
)

const rebootCheckPath = "/tmp/ota_test_should_reboot"

// Client manages the connection to the device.
type Client struct {
	deviceResolver           DeviceResolver
	sshClient                *sshutil.Client
	initialMonotonicTime     time.Time
	workaroundBrokenTimeSkip bool
	bootCounter              *uint32
}

// NewClient creates a new Client.
func NewClient(
	ctx context.Context,
	deviceResolver DeviceResolver,
	privateKey ssh.Signer,
	sshConnectBackoff retry.Backoff,
	workaroundBrokenTimeSkip bool,
	serialConn *SerialConn,
) (*Client, error) {
	sshConfig, err := newSSHConfig(privateKey)
	if err != nil {
		return nil, err
	}

	sshClient, err := sshutil.NewClient(
		ctx,
		&addrResolver{
			deviceResolver: deviceResolver,
			port:           "22",
		},
		sshConfig,
		sshConnectBackoff,
	)
	if err != nil {
		return nil, err
	}

	bootCounter := new(uint32)
	if serialConn != nil {
		go func() {
			for {
				line, err := serialConn.ReadLine()
				if err != nil {
					logger.Errorf(ctx, "failed to read from serial: %v", err)
					break
				}
				if strings.HasSuffix(line, "Welcome to Zircon\n") {
					atomic.AddUint32(bootCounter, 1)
				}
			}
		}()
	}

	c := &Client{
		deviceResolver:           deviceResolver,
		sshClient:                sshClient,
		workaroundBrokenTimeSkip: workaroundBrokenTimeSkip,
		bootCounter:              bootCounter,
	}

	if err := c.postConnectSetup(ctx); err != nil {
		c.Close()
		return nil, err

	}

	return c, nil
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

// Run all setup steps after we've connected to a device.
func (c *Client) postConnectSetup(ctx context.Context) error {
	// TODO(http://fxbug.dev/74942): The device might drop connections
	// early after boot when the RTC is updated, which typically happens
	// about 10 seconds after boot. To avoid this, if we find that we
	// connected before 15s, we'll disconnect, sleep, then connect again.
	if c.workaroundBrokenTimeSkip {
		logger.Infof(ctx, "Sleeping 15s in case fxbug.dev/74947 causes a spurious disconnection")
		time.Sleep(15 * time.Second)

		if err := c.sshClient.Reconnect(ctx); err != nil {
			return err
		}
	}

	return c.setInitialMonotonicTime(ctx)
}

func (c *Client) Reconnect(ctx context.Context) error {
	if err := c.sshClient.Reconnect(ctx); err != nil {
		return err
	}

	return c.postConnectSetup(ctx)
}

func (c *Client) setInitialMonotonicTime(ctx context.Context) error {
	var b bytes.Buffer
	cmd := []string{"/boot/bin/clock", "--monotonic"}

	// Get the device's monotonic time.
	t0 := time.Now()
	err := c.sshClient.Run(ctx, cmd, &b, os.Stderr)
	t1 := time.Now()

	if err != nil {
		c.initialMonotonicTime = time.Time{}
		return err
	}

	// Estimate the latency as half the time to execute the command.
	latency := t1.Sub(t0) / 2

	t, err := strconv.Atoi(strings.TrimSpace(b.String()))
	if err != nil {
		c.initialMonotonicTime = time.Time{}
		return err
	}

	// The output from `clock --monotonic` is in nanoseconds.
	monotonicTime := (time.Duration(t) * time.Nanosecond) - latency
	c.initialMonotonicTime = time.Now().Add(-monotonicTime)

	return nil
}

func (c *Client) getEstimatedMonotonicTime() time.Duration {
	if c.initialMonotonicTime.IsZero() {
		return 0
	}
	return time.Since(c.initialMonotonicTime)
}

// Run a command to completion on the remote device and write STDOUT and STDERR
// to the passed in io.Writers.
func (c *Client) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	return c.sshClient.Run(ctx, command, stdout, stderr)
}

// DisconnectionListener returns a channel that is closed when the client is
// disconnected.
func (c *Client) DisconnectionListener() <-chan struct{} {
	return c.sshClient.DisconnectionListener()
}

func (c *Client) GetSSHConnection(ctx context.Context) (string, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd := []string{"PATH=''", "echo", "$SSH_CONNECTION"}
	if err := c.Run(ctx, cmd, &stdout, &stderr); err != nil {
		return "", fmt.Errorf("failed to read SSH_CONNECTION: %w: %s", err, string(stderr.Bytes()))
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

// Reboot asks the device to reboot. It waits until the device reconnects
// before returning.
func (c *Client) Reboot(ctx context.Context) error {
	logger.Infof(ctx, "rebooting")

	return c.ExpectReboot(ctx, func() error {
		// Run the reboot in the background, which gives us a chance to
		// observe us successfully executing the reboot command.
		cmd := []string{"dm", "reboot", "&", "exit", "0"}
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); ok {
				logger.Infof(ctx, "ssh disconnected before returning a status")
			} else {
				return fmt.Errorf("failed to reboot: %w", err)
			}
		}

		return nil
	})
}

// RebootToRecovery asks the device to reboot into the recovery partition. It
// waits until the device disconnects before returning.
func (c *Client) RebootToRecovery(ctx context.Context) error {
	logger.Infof(ctx, "Rebooting to recovery")

	return c.ExpectDisconnect(ctx, func() error {
		// Run the reboot in the background, which gives us a chance to
		// observe us successfully executing the reboot command.
		cmd := []string{"dm", "reboot-recovery", "&", "exit", "0"}
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			// If the device rebooted before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); ok {
				logger.Infof(ctx, "ssh disconnected before returning a status")
			} else {
				return fmt.Errorf("failed to reboot into recovery: %w", err)
			}
		}

		return nil
	})
}

// Suspend asks the device to suspend. It waits until the device disconnects
// before returning.
func (c *Client) Suspend(ctx context.Context) error {
	logger.Infof(ctx, "Suspending")

	return c.ExpectDisconnect(ctx, func() error {
		// Run the suspend in the background, which gives us a chance to
		// observe us successfully executing the suspend command.
		cmd := []string{"dm", "suspend", "&", "exit", "0"}
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			// If the device suspends before ssh was able to tell
			// us the command ran, it will tell us the session
			// exited without passing along an exit code. So,
			// ignore that specific error.
			if _, ok := err.(*ssh.ExitMissingError); ok {
				logger.Infof(ctx, "ssh disconnected before returning a status")
			} else {
				return fmt.Errorf("failed to suspend: %w", err)
			}
		}

		return nil
	})
}

func (c *Client) ExpectDisconnect(ctx context.Context, f func() error) error {
	ch := c.DisconnectionListener()

	if err := f(); err != nil {
		return err
	}

	// Wait until we get a signal that we have disconnected
	select {
	case <-ch:
	case <-ctx.Done():
		return fmt.Errorf("device did not disconnect: %w", ctx.Err())
	}

	logger.Infof(ctx, "device disconnected")

	return nil
}

// ExpectReboot prepares a device for a reboot, runs a closure `f` that should
// reboot the device, then finally verifies whether a reboot actually took
// place. It does this by writing a unique value to
// `/tmp/ota_test_should_reboot`, then executing the closure. After we
// reconnect, we check if `/tmp/ota_test_should_reboot` exists. If not, exit
// with `nil`. Otherwise, we failed to reboot, or some competing test is also
// trying to reboot the device. Either way, err out.
func (c *Client) ExpectReboot(ctx context.Context, f func() error) error {
	// Generate a unique value.
	b := make([]byte, 16)
	_, err := rand.Read(b)
	if err != nil {
		return fmt.Errorf("failed to generate a unique boot number: %w", err)
	}

	// Encode the id into hex so we can write it through the shell.
	bootID := hex.EncodeToString(b)

	// Write the value to the file. Err if the file already exists by setting the
	// noclobber setting.
	cmd := fmt.Sprintf(
		`(
			set -C &&
			PATH= echo "%s" > "%s"
        )`, bootID, rebootCheckPath)

	initialBootCount := *c.bootCounter

	err = c.Run(ctx, strings.Fields(cmd), os.Stdout, os.Stderr)

	afterBootCount := *c.bootCounter

	logger.Infof(ctx, "device appears to have rebooted %d times", afterBootCount-initialBootCount)

	if initialBootCount != afterBootCount {
		return fmt.Errorf("boot counters do not match!")
	}

	if err != nil {
		return fmt.Errorf("failed to write reboot check file: %w", err)
	}

	// As a sanity check, make sure the file actually exists and has the correct
	// value.
	b, err = c.ReadRemotePath(ctx, rebootCheckPath)
	if err != nil {
		return fmt.Errorf("failed to read reboot check file: %w", err)
	}
	actual := strings.TrimSpace(string(b))

	if actual != bootID {
		return fmt.Errorf("reboot check file has wrong value: expected %q, got %q", bootID, actual)
	}

	ch := c.DisconnectionListener()

	if err := f(); err != nil {
		return err
	}

	// Wait until we get a signal that we have disconnected
	select {
	case <-ch:
	case <-ctx.Done():
		return fmt.Errorf("device did not disconnect: %w", ctx.Err())
	}

	logger.Infof(ctx, "device disconnected, waiting for device to boot")

	if err := c.Reconnect(ctx); err != nil {
		return fmt.Errorf("failed to reconnect: %w", err)
	}

	// We reconnected to the device. Check that the reboot check file doesn't exist.
	exists, err := c.RemoteFileExists(ctx, rebootCheckPath)
	if err != nil {
		return fmt.Errorf(`failed to check if %q exists: %w`, rebootCheckPath, err)
	}
	if exists {
		// The reboot file exists. This could have happened because either we
		// didn't reboot, or some other test is also trying to reboot the
		// device. We can distinguish the two by comparing the file contents
		// with the bootID we wrote earlier.
		b, err := c.ReadRemotePath(ctx, rebootCheckPath)
		if err != nil {
			return fmt.Errorf("failed to read reboot check file: %w", err)
		}
		actual := strings.TrimSpace(string(b))

		// If the contents match, then we failed to reboot.
		if actual == bootID {
			return fmt.Errorf("reboot check file exists after reboot, device did not reboot")
		}

		return fmt.Errorf(
			"reboot check file exists after reboot, and has unexpected value: expected %q, got %q",
			bootID,
			actual,
		)
	}

	logger.Infof(ctx, "device rebooted")

	return nil
}

// ValidateStaticPackages checks that all static packages have no missing blobs.
func (c *Client) ValidateStaticPackages(ctx context.Context) error {
	logger.Infof(ctx, "validating static packages")

	path := "/pkgfs/ctl/validation/missing"
	f, err := c.ReadRemotePath(ctx, path)
	if err != nil {
		return fmt.Errorf("error reading %q: %w", path, err)
	}

	merkles := strings.TrimSpace(string(f))
	if merkles != "" {
		return fmt.Errorf("static packages are missing the following blobs:\n%s", merkles)
	}

	logger.Infof(ctx, "all static package blobs are accounted for")
	return nil
}

// ReadRemotePath read a file off the remote device.
func (c *Client) ReadRemotePath(ctx context.Context, path string) ([]byte, error) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd := fmt.Sprintf(
		`(
		test -e "%s" &&
		while IFS='' read f; do
			echo "$f";
		done < "%s" &&
		if [ ! -z "$f" ];
			then echo "$f";
		fi
		)`, path, path)
	if err := c.Run(ctx, strings.Fields(cmd), &stdout, &stderr); err != nil {
		return nil, fmt.Errorf("failed to read %q: %w: %s", path, err, string(stderr.Bytes()))
	}

	return stdout.Bytes(), nil
}

// DeleteRemotePath deletes a file off the remote device.
func (c *Client) DeleteRemotePath(ctx context.Context, path string) error {
	var stderr bytes.Buffer
	cmd := []string{"PATH=''", "rm", path}
	if err := c.Run(ctx, cmd, os.Stdout, &stderr); err != nil {
		return fmt.Errorf("failed to delete %q: %w: %s", path, err, string(stderr.Bytes()))
	}

	return nil
}

// RemoteFileExists checks if a file exists on the remote device.
func (c *Client) RemoteFileExists(ctx context.Context, path string) (bool, error) {
	var stderr bytes.Buffer
	cmd := []string{"PATH=''", "test", "-e", path}

	if err := c.Run(ctx, cmd, io.Discard, &stderr); err != nil {
		if e, ok := err.(*ssh.ExitError); ok {
			if e.ExitStatus() == 1 {
				return false, nil
			}
		}
		return false, fmt.Errorf("error reading %q: %w: %s", path, err, string(stderr.Bytes()))
	}

	return true, nil
}

// RegisterPackageRepository adds the repository as a repository inside the device.
func (c *Client) RegisterPackageRepository(ctx context.Context, repo *packages.Server, createRewriteRule bool) error {
	// Test if `pkgctl repo add url` is supported on this build.
	cmd := []string{"pkgctl", "repo", "add", "url"}
	var b bytes.Buffer
	stdoutTee := io.MultiWriter(&b, os.Stdout)
	c.Run(ctx, cmd, stdoutTee, os.Stderr)
	useLegacyAmberctl := strings.Contains(b.String(), "Unrecognized argument: url")

	logger.Infof(ctx, "registering package repository: %s", repo.Dir)

	// TODO(fxbug.dev/74493): Remove amberctl path after the oldest supported build has
	// moved beyond commit 99fb3d62a991ecc19919f67201fee1d208be7f32.
	//
	// amberctl path - before pkgctl gained the 'url' argument.
	if useLegacyAmberctl {
		var subcmd string
		if createRewriteRule {
			subcmd = "add_src"
		} else {
			subcmd = "add_repo_cfg"
		}
		cmd := []string{"amberctl", subcmd, "-f", repo.URL, "-h", repo.Hash}
		return c.Run(ctx, cmd, os.Stdout, os.Stderr)
	}

	// pkgctl path - after commit 99fb3d62a991ecc19919f67201fee1d208be7f32
	// where pkgctl gained the 'url' argument.
	if createRewriteRule {
		name := "trigger-ota"
		cmd := []string{"pkgctl", "repo", "add", "url", "-n", name, "-f", "1", repo.URL}
		if err := c.Run(ctx, cmd, os.Stdout, os.Stderr); err != nil {
			return err
		}
		logger.Infof(ctx, "establishing rewriting rule for: %s", repo.URL)
		ruleTemplate := `'{"version":"1","content":[
			{"host_match":"fuchsia.com","host_replacement":"%[1]v","path_prefix_match":"/","path_prefix_replacement":"/"},
			{"host_match":"chromium.org","host_replacement":"%[1]v","path_prefix_match":"/","path_prefix_replacement":"/"}
		]}'`
		cmd = []string{"pkgctl", "rule", "replace", "json", fmt.Sprintf(ruleTemplate, name)}
		return c.Run(ctx, cmd, os.Stdout, os.Stderr)
	} else {
		cmd := []string{"pkgctl", "repo", "add", "url", "-f", "1", repo.URL}
		return c.Run(ctx, cmd, os.Stdout, os.Stderr)
	}
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
	localHostname, err := c.GetSSHConnection(ctx)
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
	logger.Infof(ctx, "connecting to sl4f")
	startTime := time.Now()

	// Ensure this client is running system_image or system_image_prime from repo.
	currentSystemImageMerkle, err := c.GetSystemImageMerkle(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to get system image merkle: %w", err)
	}
	if err := repo.VerifyMatchesAnyUpdateSystemImageMerkle(ctx, currentSystemImageMerkle); err != nil {
		return nil, fmt.Errorf("repo does not match system version: %w", err)
	}

	// Configure the target to use this repository as "fuchsia-pkg://host_target_testing_sl4f".
	repoName := "host_target_testing_sl4f"
	repoServer, err := c.ServePackageRepository(ctx, repo, repoName, true)
	if err != nil {
		return nil, fmt.Errorf("error serving repo to device: %w", err)
	}
	defer repoServer.Shutdown(ctx)

	deviceHostname, err := c.deviceResolver.ResolveName(ctx)
	if err != nil {
		return nil, fmt.Errorf("error resolving device host: %w", err)
	}

	rpcClient, err := sl4f.NewClient(ctx, c.sshClient, net.JoinHostPort(deviceHostname, "80"), "fuchsia.com")
	if err != nil {
		return nil, fmt.Errorf("error creating sl4f client: %w", err)
	}

	logger.Infof(ctx, "connected to sl4f in %s", time.Now().Sub(startTime))

	return rpcClient, nil
}

// Pave paves the device to the specified build. It assumes the device is
// already in recovery, since there are multiple ways to get a device into
// recovery. Does not reconnect to the device.
func (c *Client) Pave(ctx context.Context, build artifacts.Build) error {
	p, err := build.GetPaver(ctx)
	if err != nil {
		return fmt.Errorf("failed to get paver to pave device: %w", err)
	}

	if err := c.RebootToRecovery(ctx); err != nil {
		return fmt.Errorf("failed to reboot to recovery during paving: %w", err)
	}

	// First, pave the build's zedboot onto the device.
	logger.Infof(ctx, "waiting for device to enter zedboot")
	listeningName, err := c.deviceResolver.WaitToFindDeviceInNetboot(ctx)
	if err != nil {
		return fmt.Errorf("failed to wait for device to reboot into zedboot: %w", err)
	}

	if err = p.PaveWithOptions(ctx, listeningName, paver.Options{Mode: paver.ZedbootOnly}); err != nil {
		return fmt.Errorf("device failed to pave: %w", err)
	}

	// Next, pave the build onto the device.
	logger.Infof(ctx, "paved zedboot, waiting for the device to boot into zedboot")
	listeningName, err = c.deviceResolver.WaitToFindDeviceInNetboot(ctx)
	if err != nil {
		return fmt.Errorf("failed to wait for device to reboot into zedboot: %w", err)
	}

	if err = p.PaveWithOptions(ctx, listeningName, paver.Options{Mode: paver.SkipZedboot}); err != nil {
		return fmt.Errorf("device failed to pave: %w", err)
	}

	logger.Infof(ctx, "paver completed, waiting for device to boot")

	return nil
}

// Flash the device to the specified build. Does not reconnect to the device.
func (c *Client) Flash(ctx context.Context, build artifacts.Build) error {
	f, err := build.GetFlasher(ctx)
	if err != nil {
		return fmt.Errorf("failed to get flasher to flash device: %w", err)
	}
	defer f.Close()

	deviceHostname, err := c.deviceResolver.ResolveName(ctx)
	if err != nil {
		return fmt.Errorf("error resolving device host: %w", err)
	}
	if err := f.SetTarget(ctx, deviceHostname); err != nil {
		return fmt.Errorf("failed to add target to ffx: %w", err)
	}
	if err = f.Flash(ctx); err != nil {
		return fmt.Errorf("device failed to flash: %w", err)
	}

	logger.Infof(ctx, "flasher completed, waiting for device to boot")

	return nil
}

func (c *Client) Name() string {
	return c.deviceResolver.NodeNames()[0]
}

type addrResolver struct {
	deviceResolver DeviceResolver
	port           string
}

func (r addrResolver) Resolve(ctx context.Context) (net.Addr, error) {
	host, err := r.deviceResolver.ResolveName(ctx)
	if err != nil {
		logger.Warningf(ctx, "failed to resolve %v: %v", r.deviceResolver.NodeNames(), err)
		return nil, err
	}

	addr, err := net.ResolveTCPAddr("tcp", net.JoinHostPort(host, r.port))
	if err != nil {
		logger.Warningf(ctx, "failed to connet to %v (%v): %v", r.deviceResolver.NodeNames(), host, err)
		return nil, err
	}

	return addr, nil
}
