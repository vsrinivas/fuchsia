// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package botanist

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io/ioutil"
	"net"
	"time"

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

func ConnectViaSSH(ctx context.Context, address net.Addr, config *ssh.ClientConfig) (*ssh.Client, error) {
	network, err := network(address)
	if err != nil {
		return nil, err
	}

	var client *ssh.Client
	// TODO: figure out optimal backoff time and number of retries
	if err := retry.Retry(ctx, retry.WithMaxDuration(&retry.ZeroBackoff{}, 10*time.Second), func() error {
		var err error
		client, err = ssh.Dial(network, address.String(), config)
		return err
	}, nil); err != nil {
		return nil, fmt.Errorf("cannot connect to address %q: %v", address, err)
	}

	return client, nil
}

// SSHIntoNode connects to the device with the given nodename.
func SSHIntoNode(ctx context.Context, nodename string, config *ssh.ClientConfig) (*ssh.Client, error) {
	addr, err := GetNodeAddress(ctx, nodename, true)
	if err != nil {
		return nil, err
	}
	addr.Port = SSHPort
	return ConnectViaSSH(ctx, addr, config)
}

// DefaultSSHConfig returns a basic SSH client configuration.
func DefaultSSHConfig(privateKey []byte) (*ssh.ClientConfig, error) {
	signer, err := ssh.ParsePrivateKey(privateKey)
	if err != nil {
		return nil, err
	}
	return &ssh.ClientConfig{
		User:            sshUser,
		Auth:            []ssh.AuthMethod{ssh.PublicKeys(signer)},
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

// Returns the SSH signers associated with the key paths in the botanist config file if present.
func SSHSignersFromDeviceProperties(properties []DeviceProperties) ([]ssh.Signer, error) {
	processedKeys := make(map[string]bool)
	var signers []ssh.Signer
	for _, singleProperties := range properties {
		for _, keyPath := range singleProperties.SSHKeys {
			if !processedKeys[keyPath] {
				processedKeys[keyPath] = true
				p, err := ioutil.ReadFile(keyPath)
				if err != nil {
					return nil, err
				}
				s, err := ssh.ParsePrivateKey(p)
				if err != nil {
					return nil, err
				}
				signers = append(signers, s)
			}
		}
	}
	return signers, nil
}
