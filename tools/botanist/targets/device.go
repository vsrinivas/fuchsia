// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package targets

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"sync/atomic"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/botanist"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/serial"
	serialconstants "go.fuchsia.dev/fuchsia/tools/lib/serial/constants"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"

	"golang.org/x/crypto/ssh"
)

const (
	// Command to dump the zircon debug log over serial.
	dlogCmd = "\ndlog\n"

	// String to look for in serial log that indicates system booted. From
	// https://cs.opensource.google/fuchsia/fuchsia/+/main:zircon/kernel/top/main.cc;l=116;drc=6a0fd696cde68b7c65033da57ab911ee5db75064
	bootedLogSignature = "welcome to Zircon"
)

// DeviceConfig contains the static properties of a target device.
type DeviceConfig struct {
	// FastbootSernum is the fastboot serial number of the device.
	FastbootSernum string `json:"fastboot_sernum"`

	// Network is the network properties of the target.
	Network NetworkProperties `json:"network"`

	// SSHKeys are the default system keys to be used with the device.
	SSHKeys []string `json:"keys,omitempty"`

	// Serial is the path to the device file for serial i/o.
	Serial string `json:"serial,omitempty"`

	// SerialMux is the path to the device's serial multiplexer.
	SerialMux string `json:"serial_mux,omitempty"`
}

// NetworkProperties are the static network properties of a target.
type NetworkProperties struct {
	// Nodename is the hostname of the device that we want to boot on.
	Nodename string `json:"nodename"`

	// IPv4Addr is the IPv4 address, if statically given. If not provided, it may be
	// resolved via the netstack's MDNS server.
	IPv4Addr string `json:"ipv4"`
}

// LoadDeviceConfigs unmarshalls a slice of device configs from a given file.
func LoadDeviceConfigs(path string) ([]DeviceConfig, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read device properties file %q", path)
	}

	var configs []DeviceConfig
	if err := json.Unmarshal(data, &configs); err != nil {
		return nil, fmt.Errorf("failed to unmarshal configs: %w", err)
	}
	return configs, nil
}

// DeviceTarget represents a target device.
type DeviceTarget struct {
	*target
	config   DeviceConfig
	opts     Options
	signers  []ssh.Signer
	serial   io.ReadWriteCloser
	tftp     tftp.Client
	stopping uint32
}

// NewDeviceTarget returns a new device target with a given configuration.
func NewDeviceTarget(ctx context.Context, config DeviceConfig, opts Options) (*DeviceTarget, error) {
	// If an SSH key is specified in the options, prepend it the configs list so that it
	// corresponds to the authorized key that would be paved.
	if opts.SSHKey != "" {
		config.SSHKeys = append([]string{opts.SSHKey}, config.SSHKeys...)
	}
	signers, err := parseOutSigners(config.SSHKeys)
	if err != nil {
		return nil, fmt.Errorf("could not parse out signers from private keys: %w", err)
	}
	var s io.ReadWriteCloser
	if config.SerialMux != "" {
		if config.FastbootSernum == "" {
			s, err = serial.NewSocket(ctx, config.SerialMux)
			if err != nil {
				return nil, fmt.Errorf("unable to open: %s: %w", config.SerialMux, err)
			}
		} else {
			// We don't want to wait for the console to be ready if the device
			// is idling in Fastboot, as Fastboot does not have an interactive
			// serial console.
			s, err = serial.NewSocketWithIOTimeout(ctx, config.SerialMux, 2*time.Minute, false)
			if err != nil {
				return nil, fmt.Errorf("unable to open: %s: %w", config.SerialMux, err)
			}
		}
		// After we've made a serial connection to determine the device is ready,
		// we should close this socket since it is no longer needed. New interactions
		// with the device over serial will create new connections with the serial mux.
		s.Close()
		s = nil
	} else if config.Serial != "" {
		s, err = serial.Open(config.Serial)
		if err != nil {
			return nil, fmt.Errorf("unable to open %s: %w", config.Serial, err)
		}
	}
	base, err := newTarget(ctx, config.Network.Nodename, config.SerialMux, config.SSHKeys, s)
	if err != nil {
		return nil, err
	}
	return &DeviceTarget{
		target:  base,
		config:  config,
		opts:    opts,
		signers: signers,
		serial:  s,
	}, nil
}

// Tftp returns a tftp client interface for the device.
func (t *DeviceTarget) Tftp() tftp.Client {
	return t.tftp
}

// Nodename returns the name of the node.
func (t *DeviceTarget) Nodename() string {
	return t.config.Network.Nodename
}

// Serial returns the serial device associated with the target for serial i/o.
func (t *DeviceTarget) Serial() io.ReadWriteCloser {
	return t.serial
}

// IPv4 returns the IPv4 address of the device.
func (t *DeviceTarget) IPv4() (net.IP, error) {
	return net.ParseIP(t.config.Network.IPv4Addr), nil
}

// IPv6 returns the IPv6 of the device.
// TODO(rudymathu): Re-enable mDNS resolution of IPv6 once it is no longer
// flaky on hardware.
func (t *DeviceTarget) IPv6() (*net.IPAddr, error) {
	return nil, nil
}

// SSHKey returns the private SSH key path associated with the authorized key to be paved.
func (t *DeviceTarget) SSHKey() string {
	return t.config.SSHKeys[0]
}

// SSHClient returns an SSH client connected to the device.
func (t *DeviceTarget) SSHClient() (*sshutil.Client, error) {
	addr, err := t.IPv4()
	if err != nil {
		return nil, err
	}
	return t.sshClient(&net.IPAddr{IP: addr})
}

// Start starts the device target.
func (t *DeviceTarget) Start(ctx context.Context, images []bootserver.Image, args []string) error {
	serialSocketPath := t.SerialSocketPath()
	// Initialize the tftp client if:
	// 1. It is currently uninitialized.
	// 2. The device cannot be accessed via fastboot.
	if t.config.FastbootSernum == "" && t.tftp == nil {
		// Discover the node on the network and initialize a tftp client to
		// talk to it.
		addr, err := netutil.GetNodeAddress(ctx, t.Nodename())
		if err != nil {
			return err
		}
		tftpClient, err := tftp.NewClient(&net.UDPAddr{
			IP:   addr.IP,
			Port: tftp.ClientPort,
			Zone: addr.Zone,
		}, 0, 0)
		if err != nil {
			return err
		}
		t.tftp = tftpClient
	}

	// Set up log listener and dump kernel output to stdout.
	l, err := netboot.NewLogListener(t.Nodename())
	if err != nil {
		return fmt.Errorf("cannot listen: %w", err)
	}
	go func() {
		defer l.Close()
		for atomic.LoadUint32(&t.stopping) == 0 {
			data, err := l.Listen()
			if err != nil {
				continue
			}
			fmt.Print(data)
		}
	}()

	// Get authorized keys from the ssh signers.
	// We cannot have signers in netboot because there is no notion
	// of a hardware backed key when you are not booting from disk
	var authorizedKeys []byte
	if !t.opts.Netboot {
		if len(t.signers) > 0 {
			for _, s := range t.signers {
				authorizedKey := ssh.MarshalAuthorizedKey(s.PublicKey())
				authorizedKeys = append(authorizedKeys, authorizedKey...)
			}
		}
	}

	bootedLogChan := make(chan error)
	if serialSocketPath != "" {
		// Start searching for the string before we reboot, otherwise we can miss it.
		go func() {
			logger.Debugf(ctx, "watching serial for string that indicates device has booted: %q", bootedLogSignature)
			socket, err := net.Dial("unix", serialSocketPath)
			if err != nil {
				bootedLogChan <- fmt.Errorf("%s: %w", serialconstants.FailedToOpenSerialSocketMsg, err)
				return
			}
			defer socket.Close()
			_, err = iomisc.ReadUntilMatchString(ctx, socket, bootedLogSignature)
			bootedLogChan <- err
		}()
	}

	// Boot Fuchsia.
	if t.config.FastbootSernum != "" {
		// Copy images locally, as fastboot does not support flashing
		// from a remote location.
		// TODO(rudymathu): Transport these images via isolate for improved caching performance.
		wd, err := os.Getwd()
		if err != nil {
			return err
		}
		var imgs []*bootserver.Image
		var ffxFlashDeps []string
		if t.UseFFXExperimental(1) && !t.opts.Netboot {
			ffxFlashDeps, err = ffxutil.GetFlashDeps(wd, "fuchsia")
			if err != nil {
				return err
			}
		}
		for _, img := range images {
			img := img
			if len(ffxFlashDeps) > 0 {
				for _, dep := range ffxFlashDeps {
					if img.Path == dep {
						imgs = append(imgs, &img)
					}
				}
			} else {
				if t.neededForFlashing(&img) {
					imgs = append(imgs, &img)
				}
			}
		}
		if err := copyImagesToDir(ctx, wd, true, imgs...); err != nil {
			return err
		}

		if t.opts.Netboot {
			if err := t.ramBoot(ctx, imgs); err != nil {
				return err
			}
		} else {
			if err := t.flash(ctx, imgs); err != nil {
				return err
			}
		}
	} else {
		var imgs []bootserver.Image
		for _, img := range images {
			if t.imageOverrides != nil {
				if img.Name == fmt.Sprintf("zbi_%s", t.imageOverrides[build.ZbiImage].Name) {
					img.Args = append(img.Args, "--boot")
					imgs = append(imgs, img)
					break
				}
			} else {
				imgs = append(imgs, img)
			}
		}
		if err := bootserver.Boot(ctx, t.Tftp(), imgs, args, authorizedKeys); err != nil {
			return err
		}
	}

	if serialSocketPath != "" {
		return <-bootedLogChan
	}

	return nil
}

func getImgByName(imgs []*bootserver.Image, name string) string {
	for _, img := range imgs {
		if img.Name == name {
			return img.Path
		}
	}
	return ""
}

func (t *DeviceTarget) ramBoot(ctx context.Context, images []*bootserver.Image) error {
	// TODO(fxbug.dev/91352): Remove experimental condition once stable.
	if t.UseFFXExperimental(1) {
		zbiImageName := "zbi_zircon-a"
		vbmetaImageName := "vbmeta_zircon-a"
		if t.imageOverrides != nil {
			zbiImageName = fmt.Sprintf("zbi_%s", t.imageOverrides[build.ZbiImage].Name)
			vbmetaImageName = fmt.Sprintf("vbmeta_%s", t.imageOverrides[build.VbmetaImage].Name)
		}
		zbi := getImgByName(images, zbiImageName)
		vbmeta := getImgByName(images, vbmetaImageName)
		return t.ffx.BootloaderBoot(ctx, t.config.FastbootSernum, zbi, vbmeta, "")
	}
	bootScript := getImgByName(images, "script_fastboot-boot-script")
	if bootScript == "" {
		return errors.New("fastboot boot script not found")
	}
	cmd := exec.CommandContext(ctx, bootScript, "-s", t.config.FastbootSernum)
	stdout, stderr, flush := botanist.NewStdioWriters(ctx)
	defer flush()
	cmd.Stdout = stdout
	cmd.Stderr = stderr
	return cmd.Run()
}

func (t *DeviceTarget) writePubKey() (string, error) {
	pubkey, err := os.CreateTemp("", "pubkey*")
	if err != nil {
		return "", err
	}
	defer pubkey.Close()

	if _, err := pubkey.Write(ssh.MarshalAuthorizedKey(t.signers[0].PublicKey())); err != nil {
		return "", err
	}
	return pubkey.Name(), nil
}

func (t *DeviceTarget) flash(ctx context.Context, images []*bootserver.Image) error {
	var pubkey string
	var err error
	if len(t.signers) > 0 {
		pubkey, err = t.writePubKey()
		if err != nil {
			return err
		}
		defer os.Remove(pubkey)
	}

	// TODO(fxbug.dev/91040): Remove experimental condition once stable.
	if pubkey != "" && t.UseFFXExperimental(1) {
		flashManifest := getImgByName(images, "manifest_flash-manifest")
		if flashManifest == "" {
			return errors.New("flash manifest not found")
		}
		return t.ffx.Flash(ctx, t.config.FastbootSernum, flashManifest, pubkey)
	}

	flashScript := getImgByName(images, "script_flash-script")
	if flashScript == "" {
		return errors.New("flash script not found")
	}
	// Write the public SSH key to disk if one is needed.
	flashArgs := []string{"-s", t.config.FastbootSernum}
	if pubkey != "" {
		flashArgs = append([]string{fmt.Sprintf("--ssh-key=%s", pubkey)}, flashArgs...)
	}

	cmd := exec.CommandContext(ctx, flashScript, flashArgs...)
	stdout, stderr, flush := botanist.NewStdioWriters(ctx)
	defer flush()
	cmd.Stdout = stdout
	cmd.Stderr = stderr
	return cmd.Run()
}

// Stop stops the device.
func (t *DeviceTarget) Stop() error {
	t.target.Stop()
	atomic.StoreUint32(&t.stopping, 1)
	return nil
}

// Wait waits for the device target to stop.
func (t *DeviceTarget) Wait(context.Context) error {
	return ErrUnimplemented
}

func parseOutSigners(keyPaths []string) ([]ssh.Signer, error) {
	if len(keyPaths) == 0 {
		return nil, errors.New("must supply SSH keys in the config")
	}
	var keys [][]byte
	for _, keyPath := range keyPaths {
		p, err := os.ReadFile(keyPath)
		if err != nil {
			return nil, fmt.Errorf("could not read SSH key file %q: %w", keyPath, err)
		}
		keys = append(keys, p)
	}

	var signers []ssh.Signer
	for _, p := range keys {
		signer, err := ssh.ParsePrivateKey(p)
		if err != nil {
			return nil, err
		}
		signers = append(signers, signer)
	}
	return signers, nil
}

func (t *DeviceTarget) neededForFlashing(img *bootserver.Image) bool {
	zbiImageName := "zbi_zircon-a"
	vbmetaImageName := "vbmeta_zircon-a"
	if t.imageOverrides != nil {
		zbiImageName = fmt.Sprintf("zbi_%s", t.imageOverrides[build.ZbiImage].Name)
		vbmetaImageName = fmt.Sprintf("vbmeta_%s", t.imageOverrides[build.VbmetaImage].Name)
	}
	neededImages := []string{zbiImageName, vbmetaImageName, "script_flash-script", "exe.linux-x64_fastboot", "script_fastboot-boot-script", "manifest_flash-manifest"}
	if img.IsFlashable {
		return true
	}
	for _, imageName := range neededImages {
		if img.Name == imageName {
			return true
		}
	}
	return false
}
