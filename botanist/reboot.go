// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"time"

	"fuchsia.googlesource.com/tools/botanist/pdu/amt"
	"fuchsia.googlesource.com/tools/botanist/pdu/wol"
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
)

// Config represents a PDU configuration for a particular device.
type Config struct {
	// Type is the type of PDU to use.
	Type string `json:"type"`

	// Host is the network hostname of the PDU e.g. fuchsia-tests-pdu-001.
	Host string `json:"host"`

	// HostHwAddr is the ethernet MAC address of the PDU e.g. 10:10:10:10:10:10
	HostMACAddr string `json:"host_mac_addr"`

	// Username is the username used to log in to the PDU.
	Username string `json:"username"`

	// Password is the password used to log in to the PDU.
	Password string `json:"password"`
}

// RebootDevice attempts to reboot the specified device into recovery, and
// additionally uses the given configuration to reboot the device if specified.
func RebootDevice(cfg *Config, signers []ssh.Signer, nodename string) error {
	// Always attempt to soft reboot the device to recovery.
	err := rebootRecovery(nodename, signers)

	// Hard reboot the device if specified in the config.
	switch cfg.Type {
	case "amt":
		return amt.Reboot(cfg.Host, cfg.Username, cfg.Password)
	case "wol":
		return wol.Reboot(botBroadcastAddr, botInterface, cfg.HostMACAddr)
	}

	return err
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
