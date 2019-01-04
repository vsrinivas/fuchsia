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
)

// GenerateKeyPair generates a pair of private/public keys.
func GenerateKeyPair(bitSize int) ([]byte, []byte, error) {
	key, err := rsa.GenerateKey(rand.Reader, bitSize)
	if err != nil {
		return nil, nil, err
	}

	pubkey, err := ssh.NewPublicKey(&key.PublicKey)
	if err != nil {
		return nil, nil, err
	}
	pembuf := pubkey.Marshal()

	var privateKey = &pem.Block{
		Type:  "RSA PRIVATE KEY",
		Bytes: x509.MarshalPKCS1PrivateKey(key),
	}
	buf := pem.EncodeToMemory(privateKey)

	return pembuf, buf, nil
}

func ConnectSSH(ctx context.Context, address net.Addr, config *ssh.ClientConfig) (*ssh.Client, error) {
	network, err := network(address)
	if err != nil {
		return nil, err
	}

	var client *ssh.Client

	// TODO: figure out optimal backoff time and number of retries
	if err := retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 10), func() error {
		var err error
		client, err = ssh.Dial(network, address.String(), config)
		return err
	}, nil); err != nil {
		return nil, fmt.Errorf("cannot connect to address '%s': %v", address, err)
	}

	return client, nil
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
