// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"net"
	"time"

	"fuchsia.googlesource.com/tools/netutil"
	"fuchsia.googlesource.com/tools/retry"

	"golang.org/x/crypto/ssh"
)

const (
	// Default SSH server port.
	SSHPort = 22

	// Default RSA key size.
	RSAKeySize = 2048

	// The default timeout for IO operations.
	defaultIOTimeout = 5 * time.Second

	sshUser = "fuchsia"
)

// GeneratePrivateKey generates a private SSH key.
func GeneratePrivateKey() ([]byte, error) {
	key, err := rsa.GenerateKey(rand.Reader, RSAKeySize)
	if err != nil {
		return nil, err
	}
	privateKey := &pem.Block{
		Type:  "RSA PRIVATE KEY",
		Bytes: x509.MarshalPKCS1PrivateKey(key),
	}
	buf := pem.EncodeToMemory(privateKey)

	return buf, nil
}

func Connect(ctx context.Context, address net.Addr, config *ssh.ClientConfig) (*ssh.Client, error) {
	network, err := network(address)
	if err != nil {
		return nil, err
	}

	var client *ssh.Client
	// TODO: figure out optimal backoff time and number of retries
	if err := retry.Retry(ctx, retry.WithMaxDuration(&retry.ZeroBackoff{}, time.Minute), func() error {
		var err error
		client, err = ssh.Dial(network, address.String(), config)
		return err
	}, nil); err != nil {
		return nil, fmt.Errorf("cannot connect to address %q: %v", address, err)
	}

	return client, nil
}

// ConnectToNode connects to the device with the given nodename.
func ConnectToNode(ctx context.Context, nodename string, config *ssh.ClientConfig) (*ssh.Client, error) {
	addr, err := netutil.GetNodeAddress(ctx, nodename, true)
	if err != nil {
		return nil, err
	}
	addr.Port = SSHPort
	return Connect(ctx, addr, config)
}

// DefaultSSHConfig returns a basic SSH client configuration.
func DefaultSSHConfig(privateKey []byte) (*ssh.ClientConfig, error) {
	signer, err := ssh.ParsePrivateKey(privateKey)
	if err != nil {
		return nil, err
	}
	return DefaultSSHConfigFromSigners(signer)
}

// DefaultSSHConfigFromSigners returns a basic SSH client configuration.
func DefaultSSHConfigFromSigners(signers ...ssh.Signer) (*ssh.ClientConfig, error) {
	return &ssh.ClientConfig{
		User:            sshUser,
		Auth:            []ssh.AuthMethod{ssh.PublicKeys(signers...)},
		Timeout:         defaultIOTimeout,
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}, nil
}

// Returns the network to use to SSH into a device.
func network(address net.Addr) (string, error) {
	var ip *net.IP

	// We need these type assertions because the net package (annoyingly) doesn't provide
	// an interface for objects that have an IP address.
	switch addr := address.(type) {
	case *net.UDPAddr:
		ip = &addr.IP
	case *net.TCPAddr:
		ip = &addr.IP
	case *net.IPAddr:
		ip = &addr.IP
	default:
		return "", fmt.Errorf("unsupported address type: %T", address)
	}

	if ip.To4() != nil {
		return "tcp", nil // IPv4
	}
	if ip.To16() != nil {
		return "tcp6", nil // IPv6
	}
	return "", fmt.Errorf("cannot infer network for IP address %s", ip.String())
}
