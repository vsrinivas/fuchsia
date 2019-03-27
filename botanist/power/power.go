// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package power

import (
	"context"
	"fmt"
	"time"

	"fuchsia.googlesource.com/tools/botanist/power/amt"
	"fuchsia.googlesource.com/tools/botanist/power/wol"
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

	sshUser = "fuchsia"

	sshRebootTimeout = 10 * time.Second
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

// RebootDevice attempts to reboot the specified device into recovery, and
// additionally uses the given configuration to reboot the device if specified.
func (c Client) RebootDevice(signers []ssh.Signer, nodename string) error {
	// Always attempt to soft reboot the device to recovery.
	ch := make(chan error)
	go func() {
		ch <- rebootRecovery(nodename, signers)
	}()
	// Just move on if rebootRecovery returns or timeout reached.
	select {
	case <-ch:
	// Strict timeout if SSH hangs
	case <-time.After(sshRebootTimeout):
	}

	// Hard reboot the device if specified in the config.
	switch c.Type {
	case "amt":
		return amt.Reboot(c.Host, c.Username, c.Password)
	case "wol":
		return wol.Reboot(botBroadcastAddr, botInterface, c.HostMACAddr)
	default:
		return fmt.Errorf("unsupported power type: %s", c.Type)
	}
}

func rebootRecovery(nodeName string, signers []ssh.Signer) error {
	return sendCommand(nodeName, "dm reboot-recovery", signers)
}

func sendCommand(nodeName, command string, signers []ssh.Signer) error {
	publicKeyAuth := ssh.PublicKeys(signers...)
	config := &ssh.ClientConfig{
		User:            sshUser,
		Auth:            []ssh.AuthMethod{publicKeyAuth},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}

	ctx := context.Background()
	client, err := sshutil.ConnectToNode(ctx, nodeName, config)
	if err != nil {
		return err
	}

	defer client.Close()

	session, err := client.NewSession()
	if err != nil {
		return err
	}

	defer session.Close()

	err = session.Start(command)
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
	// session.Wait() doesn't return on success due to the reboot killing the
	// connection on the target nor does it timeout on any reasonable time
	// scale.
	case <-time.After(10 * time.Second):
		return nil
	}
}
