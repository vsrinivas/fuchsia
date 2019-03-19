// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"io/ioutil"

	"golang.org/x/crypto/ssh"
)

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
