// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package power

import (
	"context"
	"io"

	"fuchsia.googlesource.com/tools/botanist/power/amt"
	"fuchsia.googlesource.com/tools/botanist/power/wol"
	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/sshutil"

	"golang.org/x/crypto/ssh"
)

// TODO(IN-977) Clean this up per suggestions in go/fxr/251550

const (
	// Controller machines use 192.168.42.1/24 for swarming bots
	// This will broadcast to that entire subnet.
	botBroadcastAddr = "192.168.42.255:9"

	// Controller machines have multiple interfaces, currently
	// 'eno2' is used for swarming bots.
	botInterface = "eno2"
)

// Client represents a power management configuration for a particular device.
type Client struct {
	// Type is the type of manager to use.
	Type string `json:"type"`

	// Host is the network hostname of the manager, e.g. fuchsia-tests-pdu-001.
	Host string `json:"host"`

	// HostHwAddr is the ethernet MAC address of the manager,  e.g. 10:10:10:10:10:10
	HostMACAddr string `json:"host_mac_addr"`

	// Username is the username used to log in to the manager.
	Username string `json:"username"`

	// Password is the password used to log in to the manager..
	Password string `json:"password"`
}

type Rebooter interface {
	reboot() error
}

type SshRebooter struct {
	nodename string
	signers  []ssh.Signer
}

type SerialRebooter struct {
	serial io.ReadWriter
}

// RebootDevice attempts to reboot the specified device into recovery, and
// additionally uses the given configuration to reboot the device if specified.
func (c Client) RebootDevice(signers []ssh.Signer, nodename string, serial io.ReadWriter) error {
	var rebooter Rebooter
	if serial != nil {
		rebooter = NewSerialRebooter(serial)
	} else {
		rebooter = NewSSHRebooter(nodename, signers)
	}
	// Always attempt to soft reboot the device to recovery.
	err := rebooter.reboot()
	if err != nil {
		logger.Warningf(context.Background(), "soft reboot failed: %v", err)
	}

	// Hard reboot the device if specified in the config.
	switch c.Type {
	case "amt":
		return amt.Reboot(c.Host, c.Username, c.Password)
	case "wol":
		return wol.Reboot(botBroadcastAddr, botInterface, c.HostMACAddr)
	default:
		return err
	}
}

func NewSerialRebooter(serial io.ReadWriter) *SerialRebooter {
	return &SerialRebooter{
		serial: serial,
	}
}

func NewSSHRebooter(nodename string, signers []ssh.Signer) *SshRebooter {
	return &SshRebooter{
		nodename: nodename,
		signers:  signers,
	}
}

func (s *SerialRebooter) reboot() error {
	_, err := io.WriteString(s.serial, "\ndm reboot-recovery\n")
	return err
}

func (s *SshRebooter) reboot() error {
	config, err := sshutil.DefaultSSHConfigFromSigners(s.signers...)
	if err != nil {
		return err
	}

	ctx := context.Background()
	client, err := sshutil.ConnectToNode(ctx, s.nodename, config)
	if err != nil {
		return err
	}

	defer client.Close()

	session, err := client.NewSession()
	if err != nil {
		return err
	}

	defer session.Close()

	// Invoke `dm reboot-recovery` with a 2 second delay in the background, then exit the SSH shell.
	// This prevents the SSH connection from hanging waiting for `dm reboot-recovery` to return.
	err = session.Start("{ sleep 2; dm reboot-recovery; } >/dev/null & exit")
	if err != nil {
		return err
	}

	done := make(chan error)
	go func() {
		done <- session.Wait()
	}()

	select {
	case err := <-done:
		return err
	}
}
