// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package targets

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/repo"
	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/serial"
	"go.fuchsia.dev/fuchsia/tools/lib/syslog"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"golang.org/x/sync/errgroup"
)

const (
	localhostPlaceholder = "localhost"
	repoID               = "fuchsia-pkg://fuchsia.com"
)

// FFXInstance is a wrapper around ffxutil.FFXInstance with extra fields to
// determine how botanist uses ffx.
type FFXInstance struct {
	*ffxutil.FFXInstance
	// ExperimentLevel specifies what level of experimental ffx features
	// to enable.
	ExperimentLevel int
}

// Target represents a generic fuchsia instance.
type Target interface {
	// AddPackageRepository adds a given package repository to the target.
	AddPackageRepository(client *sshutil.Client, repoURL, blobURL string) error

	// CaptureSerialLog starts capturing serial logs to the given file.
	// This is only valid once the target has a serial multiplexer running.
	CaptureSerialLog(filename string) error

	// CaptureSyslog starts capturing the syslog to the given file.
	// This is only valid when the target has SSH running.
	CaptureSyslog(client *sshutil.Client, filename, repoURL, blobURL string) error

	// IPv4 returns the IPv4 of the target; this is nil unless explicitly
	// configured.
	IPv4() (net.IP, error)

	// IPv6 returns the IPv6 of the target.
	IPv6() (*net.IPAddr, error)

	// Nodename returns the name of the target node.
	Nodename() string

	// Serial returns the serial device associated with the target for serial i/o.
	Serial() io.ReadWriteCloser

	// SerialSocketPath returns the path to the target's serial socket.
	SerialSocketPath() string

	// SSHClient returns an SSH client to the device (if the device has SSH running).
	SSHClient() (*sshutil.Client, error)

	// SSHKey returns the private key corresponding an authorized SSH key of the target.
	SSHKey() string

	// Start starts the target.
	Start(ctx context.Context, images []bootserver.Image, args []string) error

	// StartSerialServer starts the serial server for the target iff one
	// does not exist.
	StartSerialServer() error

	// Stop stops the target.
	Stop() error

	// Wait waits for the target to finish running.
	Wait(context.Context) error

	// SetFFX attaches an ffx instance and env to the target.
	SetFFX(*FFXInstance, []string)

	// GetFFX returns the ffx instance associated with the target.
	GetFFX() *FFXInstance

	// UseFFX returns whether to enable using ffx.
	UseFFX() bool

	// UseFFXExperimental returns whether to enable an experimental ffx feature.
	UseFFXExperimental(int) bool

	// FFXEnv returns the env vars that the ffx instance should run with
	FFXEnv() []string

	// SetImageOverrides sets the images to override the defaults in images.json.
	SetImageOverrides(build.ImageOverrides)
}

// target is a generic Fuchsia instance.
// It is not intended to be instantiated directly, but rather embedded into a
// more concrete implementation.
type target struct {
	targetCtx       context.Context
	targetCtxCancel context.CancelFunc

	nodename     string
	serial       io.ReadWriteCloser
	serialSocket string
	sshKeys      []string

	ipv4         net.IP
	ipv6         *net.IPAddr
	serialServer *serial.Server

	ffx    *FFXInstance
	ffxEnv []string

	imageOverrides build.ImageOverrides
}

// newTarget creates a new generic Fuchsia target.
func newTarget(ctx context.Context, nodename, serialSocket string, sshKeys []string, serial io.ReadWriteCloser) (*target, error) {
	targetCtx, cancel := context.WithCancel(ctx)
	t := &target{
		targetCtx:       targetCtx,
		targetCtxCancel: cancel,

		nodename:     nodename,
		serial:       serial,
		serialSocket: serialSocket,
		sshKeys:      sshKeys,
	}
	return t, nil
}

// SetFFX attaches an FFXInstance and environment to the target.
func (t *target) SetFFX(ffx *FFXInstance, env []string) {
	t.ffx = ffx
	t.ffxEnv = env
}

// GetFFX returns the FFXInstance associated with the target.
func (t *target) GetFFX() *FFXInstance {
	return t.ffx
}

// UseFFX returns true if there is an FFXInstance associated with this target.
// Use to enable stable ffx features.
func (t *target) UseFFX() bool {
	return t.ffx != nil && t.ffx.FFXInstance != nil
}

// UseFFXExperimental returns true if there is an FFXInstance associated with
// this target and we're running with an experiment level >= the provided level.
// Use to enable experimental ffx features.
func (t *target) UseFFXExperimental(level int) bool {
	return t.UseFFX() && t.ffx.ExperimentLevel >= level
}

// FFXEnv returns the environment to run ffx with.
func (t *target) FFXEnv() []string {
	return t.ffxEnv
}

// SetImageOverrides sets the images to override the defaults in images.json.
func (t *target) SetImageOverrides(images build.ImageOverrides) {
	t.imageOverrides = images
}

// StartSerialServer spawns a new serial server fo the given target.
// This is a no-op if a serial socket already exists, or if there is
// no attached serial device.
func (t *target) StartSerialServer() error {
	// We have to no-op instead of returning an error as there are code
	// paths that directly write to the serial log using QEMU's chardev
	// flag, and throwing an error here would break those paths.
	if t.serial == nil || t.serialSocket != "" {
		return nil
	}
	t.serialSocket = createSocketPath()
	t.serialServer = serial.NewServer(t.serial, serial.ServerOptions{})
	addr := &net.UnixAddr{
		Name: t.serialSocket,
		Net:  "unix",
	}
	l, err := net.ListenUnix("unix", addr)
	if err != nil {
		return err
	}
	go func() {
		t.serialServer.Run(t.targetCtx, l)
	}()
	return nil
}

// resolveIP uses mDNS to resolve the IPv6 and IPv4 addresses of the
// target. It then caches the results so future requests are fast.
func (t *target) resolveIP() error {
	ctx, cancel := context.WithTimeout(t.targetCtx, 2*time.Minute)
	defer cancel()
	ipv4, ipv6, err := resolveIP(ctx, t.nodename)
	if err != nil {
		return err
	}
	t.ipv4 = ipv4
	t.ipv6 = &ipv6
	return nil
}

// SerialSocketPath returns the path to the unix socket multiplexing serial
// logs.
func (t *target) SerialSocketPath() string {
	return t.serialSocket
}

// IPv4 returns the IPv4 address of the target.
func (t *target) IPv4() (net.IP, error) {
	if t.ipv4 == nil {
		if err := t.resolveIP(); err != nil {
			return nil, err
		}
	}
	return t.ipv4, nil
}

// IPv6 returns the IPv6 address of the target.
func (t *target) IPv6() (*net.IPAddr, error) {
	if t.ipv6 == nil {
		if err := t.resolveIP(); err != nil {
			return nil, err
		}
	}
	return t.ipv6, nil
}

// CaptureSerialLog starts copying serial logs from the serial server
// to the given filename. This is a blocking function; it will not return
// until either the serial server disconnects or the target is stopped.
func (t *target) CaptureSerialLog(filename string) error {
	if t.serialSocket == "" {
		return errors.New("CaptureSerialLog() failed; serialSocket was empty")
	}
	serialLog, err := os.Create(filename)
	if err != nil {
		return err
	}
	conn, err := net.Dial("unix", t.serialSocket)
	if err != nil {
		return err
	}
	// Set up a goroutine to terminate this capture on target context cancel.
	go func() {
		<-t.targetCtx.Done()
		conn.Close()
		serialLog.Close()
	}()

	// Start capturing serial logs.
	b := bufio.NewReader(conn)
	for {
		line, err := b.ReadString('\n')
		if err != nil {
			if !errors.Is(err, net.ErrClosed) {
				return fmt.Errorf("%s: %w", constants.SerialReadErrorMsg, err)
			}
			return nil
		}
		if _, err := io.WriteString(serialLog, line); err != nil {
			return fmt.Errorf("failed to write line to serial log: %w", err)
		}
	}
}

// sshClient is a helper function that returns an SSH client connected to the
// target, which can be found at the given address.
func (t *target) sshClient(addr *net.IPAddr) (*sshutil.Client, error) {
	if len(t.sshKeys) == 0 {
		return nil, errors.New("SSHClient() failed; no ssh keys provided")
	}

	p, err := os.ReadFile(t.sshKeys[0])
	if err != nil {
		return nil, err
	}
	config, err := sshutil.DefaultSSHConfig(p)
	if err != nil {
		return nil, err
	}
	return sshutil.NewClient(
		t.targetCtx,
		sshutil.ConstantAddrResolver{
			Addr: &net.TCPAddr{
				IP:   addr.IP,
				Zone: addr.Zone,
				Port: sshutil.SSHPort,
			},
		},
		config,
		sshutil.DefaultConnectBackoff(),
	)
}

// AddPackageRepository adds the given package repository to the target.
func (t *target) AddPackageRepository(client *sshutil.Client, repoURL, blobURL string) error {
	localhost := strings.Contains(repoURL, localhostPlaceholder) || strings.Contains(blobURL, localhostPlaceholder)
	lScopedRepoURL := repoURL
	if localhost {
		localAddr := client.LocalAddr()
		if localAddr == nil {
			return fmt.Errorf("failed to get local addr for ssh client")
		}
		host := localScopedLocalHost(localAddr.String())
		lScopedRepoURL = strings.Replace(repoURL, localhostPlaceholder, host, 1)
		logger.Infof(t.targetCtx, "local-scoped package repository address: %s\n", lScopedRepoURL)
	}

	rScopedRepoURL := repoURL
	rScopedBlobURL := blobURL
	if localhost {
		host, err := remoteScopedLocalHost(t.targetCtx, client)
		if err != nil {
			return err
		}
		rScopedRepoURL = strings.Replace(repoURL, localhostPlaceholder, host, 1)
		logger.Infof(t.targetCtx, "remote-scoped package repository address: %s\n", rScopedRepoURL)
		rScopedBlobURL = strings.Replace(blobURL, localhostPlaceholder, host, 1)
		logger.Infof(t.targetCtx, "remote-scoped package blob address: %s\n", rScopedBlobURL)
	}

	rootMeta, err := repo.GetRootMetadataInsecurely(t.targetCtx, lScopedRepoURL)
	if err != nil {
		return fmt.Errorf("failed to derive root metadata: %w", err)
	}

	cfg := &repo.Config{
		URL:           repoID,
		RootKeys:      rootMeta.RootKeys,
		RootVersion:   rootMeta.RootVersion,
		RootThreshold: rootMeta.RootThreshold,
		Mirrors: []repo.MirrorConfig{
			{
				URL:     rScopedRepoURL,
				BlobURL: rScopedBlobURL,
			},
		},
	}

	return repo.AddFromConfig(t.targetCtx, client, cfg)
}

// CaptureSyslog collects the target's syslog in the given file.
// This requires SSH to be running on the target. We pass the repoURL
// and blobURL of the package repo as a matter of convenience - it makes
// it easy to re-register the package repository on reboot. This function
// blocks until the target is stopped.
func (t *target) CaptureSyslog(client *sshutil.Client, filename, repoURL, blobURL string) error {
	syslogger := syslog.NewSyslogger(client)

	f, err := os.Create(filename)
	if err != nil {
		return err
	}
	defer f.Close()

	errs := syslogger.Stream(t.targetCtx, f)
	for range errs {
		if !syslogger.IsRunning() {
			return nil
		}
		// TODO(rudymathu): This is a bit of a hack that results from the fact that
		// we don't know when test binaries restart the device. Eventually, we should
		// build out a more resilient framework in which we register "restart handlers"
		// that are triggered on reboot.
		if repoURL != "" && blobURL != "" {
			t.AddPackageRepository(client, repoURL, blobURL)
		}
	}
	return nil
}

// Stop cleans up all of the resources used by the target.
func (t *target) Stop() {
	// Cancelling the target context will stop any background goroutines.
	// This includes serial/syslog capture and any serial servers that may
	// be running.
	t.targetCtxCancel()
}

func copyImagesToDir(ctx context.Context, dir string, preservePath bool, imgs ...*bootserver.Image) error {
	// Copy each in a goroutine for efficiency's sake.
	eg, ctx := errgroup.WithContext(ctx)
	for _, img := range imgs {
		if img.Reader != nil {
			img := img
			eg.Go(func() error {
				base := img.Name
				if preservePath {
					base = img.Path
				}
				dest := filepath.Join(dir, base)
				return bootserver.DownloadWithRetries(ctx, dest, func() error {
					return copyImageToDir(ctx, dest, img)
				})
			})
		}
	}
	return eg.Wait()
}

func copyImageToDir(ctx context.Context, dest string, img *bootserver.Image) error {
	f, ok := img.Reader.(*os.File)
	if ok {
		if err := osmisc.CopyFile(f.Name(), dest); err != nil {
			return err
		}
		img.Path = dest
		return nil
	}

	f, err := osmisc.CreateFile(dest)
	if err != nil {
		return err
	}
	defer f.Close()

	// Log progress to avoid hitting I/O timeout in case of slow transfers.
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	go func() {
		for range ticker.C {
			logger.Debugf(ctx, "transferring %s...\n", img.Name)
		}
	}()

	if _, err := io.Copy(f, iomisc.ReaderAtToReader(img.Reader)); err != nil {
		return fmt.Errorf("%s (%q): %w", constants.FailedToCopyImageMsg, img.Name, err)
	}
	img.Path = dest

	if img.IsExecutable {
		if err := os.Chmod(img.Path, os.ModePerm); err != nil {
			return fmt.Errorf("failed to make %s executable: %w", img.Path, err)
		}
	}

	// We no longer need the reader at this point.
	if c, ok := img.Reader.(io.Closer); ok {
		c.Close()
	}
	img.Reader = nil
	return nil
}

func localScopedLocalHost(laddr string) string {
	tokens := strings.Split(laddr, ":")
	host := strings.Join(tokens[:len(tokens)-1], ":") // Strips the port.
	return escapePercentSign(host)
}

func remoteScopedLocalHost(ctx context.Context, client *sshutil.Client) (string, error) {
	// From the ssh man page:
	// "SSH_CONNECTION identifies the client and server ends of the connection.
	// The variable contains four space-separated values: client IP address,
	// client port number, server IP address, and server port number."
	// We wish to obtain the client IP address, as will be scoped from the
	// remote address.
	var stdout bytes.Buffer
	if err := client.Run(ctx, []string{"echo", "${SSH_CONNECTION}"}, &stdout, nil); err != nil {
		return "", fmt.Errorf("failed to derive $SSH_CONNECTION: %w", err)
	}
	val := stdout.String()
	tokens := strings.Split(val, " ")
	if len(tokens) != 4 {
		return "", fmt.Errorf("$SSH_CONNECTION should be four space-separated values and not %q", val)
	}
	host := tokens[0]
	// If the host is an IPv6 address with a zone, surround it with brackets
	// and escape the percent sign.
	if strings.Contains(host, "%") {
		host = "[" + escapePercentSign(host) + "]"
	}
	return host, nil
}

// From the spec https://tools.ietf.org/html/rfc6874#section-2:
// "%" is always treated as an escape character in a URI, so, according to
// the established URI syntax any occurrences of literal "%" symbols in a
// URI MUST be percent-encoded and represented in the form "%25".
func escapePercentSign(addr string) string {
	if strings.Contains(addr, "%25") {
		return addr
	}
	return strings.Replace(addr, "%", "%25", 1)
}

func createSocketPath() string {
	// We randomly construct a socket path that is highly improbable to collide with anything.
	randBytes := make([]byte, 16)
	rand.Read(randBytes)
	return filepath.Join(os.TempDir(), "serial"+hex.EncodeToString(randBytes)+".sock")
}

// Options represents lifecycle options for a target. The options will not necessarily make
// sense for all target types.
type Options struct {
	// Netboot gives whether to netboot or pave. Netboot here is being used in the
	// colloquial sense of only sending netsvc a kernel to mexec. If false, the target
	// will be paved. Ignored for QEMUTarget.
	Netboot bool

	// SSHKey is a private SSH key file, corresponding to an authorized key to be paved or
	// to one baked into a boot image.
	SSHKey string
}

// DeriveTarget returns a Target based on the obj json and opts.
func DeriveTarget(ctx context.Context, obj []byte, opts Options) (Target, error) {
	type typed struct {
		Type string `json:"type"`
	}
	var x typed

	if err := json.Unmarshal(obj, &x); err != nil {
		return nil, fmt.Errorf("object in list has no \"type\" field: %w", err)
	}
	switch x.Type {
	case "aemu":
		var cfg QEMUConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid QEMU config found: %w", err)
		}
		return NewAEMUTarget(ctx, cfg, opts)
	case "qemu":
		var cfg QEMUConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid QEMU config found: %w", err)
		}
		return NewQEMUTarget(ctx, cfg, opts)
	case "device":
		var cfg DeviceConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid device config found: %w", err)
		}
		t, err := NewDeviceTarget(ctx, cfg, opts)
		return t, err
	case "gce":
		var cfg GCEConfig
		if err := json.Unmarshal(obj, &cfg); err != nil {
			return nil, fmt.Errorf("invalid GCE config found: %w", err)
		}
		return NewGCETarget(ctx, cfg, opts)
	default:
		return nil, fmt.Errorf("unknown type found: %q", x.Type)
	}
}

// StartOptions boundle together some options that might change how a target
// is started.
type StartOptions struct {
	// Netboot tells the image to use Netboot or to Pave
	Netboot bool

	// ImageManifest is the path to an image manifest
	ImageManifest string

	// ZirconArgs are kernel command-line arguments to pass on boot.
	ZirconArgs []string
}

// StartTargets starts all the targets in targetSlice given the opts.
func StartTargets(ctx context.Context, opts StartOptions, targetSlice []Target) error {
	bootMode := bootserver.ModePave
	if opts.Netboot {
		bootMode = bootserver.ModeNetboot
	}

	// Check the first target to see if ffx is enabled. All targets share the same ffx daemon,
	// so we can use the ffx associated with the first target to set config values.
	if len(targetSlice) > 0 && targetSlice[0].UseFFXExperimental(1) {
		ffx := targetSlice[0].GetFFX()
		if err := ffx.ConfigSet(ctx, "ffx.fastboot.inline_target", "true"); err != nil {
			return err
		}
		// Setting the `ffx.fastboot.inline_target` field causes ffx to assume the target
		// it's trying to reach is in fastboot mode. We need to reset it to false after
		// we're done flashing.
		defer func() {
			if err := ffx.ConfigSet(ctx, "ffx.fastboot.inline_target", "false"); err != nil {
				logger.Errorf(ctx, "failed to reset ffx.fastboot.inline_target to false")
			}
		}()
	}
	// TODO(https://fxbug.dev/111922): Remove following comment once we don't run a subcommand.
	// We wait until targets have started before running the subcommand against the zeroth one.
	eg, startCtx := errgroup.WithContext(ctx)
	for _, t := range targetSlice {
		t := t
		eg.Go(func() error {
			// TODO(fxbug.dev/47910): Move outside gofunc once we get rid of downloading or ensure that it only happens once.
			imgs, closeFunc, err := bootserver.GetImages(startCtx, opts.ImageManifest, bootMode)
			if err != nil {
				return err
			}
			defer closeFunc()

			return t.Start(startCtx, imgs, opts.ZirconArgs)
		})
	}
	return eg.Wait()
}

// StopTargets stop all the targets in targetSlice
func StopTargets(ctx context.Context, targetSlice []Target) {
	// Stop the targets in parallel.
	var eg errgroup.Group
	for _, t := range targetSlice {
		t := t
		eg.Go(func() error {
			return t.Stop()
		})
	}
	_ = eg.Wait()
}
