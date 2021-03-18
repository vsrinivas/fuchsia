// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CLI implements the CIPD interface by interacting with the CIPD binary that
// is already installed on the local machine, by using the local shell.

package testdriver

import (
	"bytes"
	"fmt"
	"log"
	"os"
	"os/exec"
	"strings"
)

type CLI struct {
	env []string
}

func NewCLI() *CLI {
	return &CLI{
		env: os.Environ(),
	}
}

// Find the CIPD version (a very long string of characters) of a given package.
func (c *CLI) GetVersion(pkg string, tags []*Tag, refs []*Ref) (PkgInstance, error) {
	query := pkg
	for _, t := range tags {
		query = fmt.Sprintf("%s -tag %s", query, t)
	}
	for _, r := range refs {
		query = fmt.Sprintf("%s -ref %s", query, r)
	}

	pkgInstance := PkgInstance{
		name: pkg,
	}
	response, err := c.search(query)
	if err != nil {
		return pkgInstance, err
	}

	// Results will look like the following:
	//
	// Instances:
	//   fuchsia/sdk/gn/linux-amd64:TS6d3yJDtdARsCoxC75HkRLinNH0ABoJYVEfz575u-QC
	//
	// Extract the instance ID, which is the text after the ":" character.
	cipdVersions := []string{}
	for _, line := range strings.Split(response, "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, pkg) {
			cipdVersions = append(cipdVersions, strings.Split(line, ":")[1])
		}
	}
	if len(cipdVersions) != 1 {
		return pkgInstance, fmt.Errorf("Expected to find 1 CIPD version for package %s, found %d\n", pkg, len(cipdVersions))
	}

	pkgInstance.version = cipdVersions[0]

	return pkgInstance, nil
}

// Download retrieves the specified package at the specified CIPD version, and
// installs it in the location specified by "dest".
func (c *CLI) Download(pkg PkgInstance, dest string) error {
	log.Printf("Downloading %s (version %v)...\n", pkg.name, pkg.version)

	args := strings.Split(fmt.Sprintf("install %s %s -root %s", pkg.name, pkg.version, dest), " ")
	stdout, stderr, err := c.run("cipd", args)
	if err != nil {
		return fmt.Errorf("%v\n\n %s\n%s\n", err, stdout, stderr)
	}

	expectedExists := fmt.Sprintf("Package %s is up-to-date", pkg)
	expectedDownloaded := fmt.Sprintf("Deployed %s:%s", pkg.name, pkg.version)
	combinedOutput := fmt.Sprintf("%s\n%s", stdout, stderr)
	if strings.Contains(combinedOutput, expectedExists) || strings.Contains(combinedOutput, expectedDownloaded) {
		return nil
	}
	return fmt.Errorf("An expected success message (%s || %s) not found in output: %s\n%s\n", expectedExists, expectedDownloaded, stdout, stderr)
}

// search executes a CIPD search command given the provided query.
func (c *CLI) search(query string) (string, error) {
	args := strings.Split(fmt.Sprintf("search %s", query), " ")
	stdout, stderr, err := c.run("cipd", args)
	if err != nil {
		return "", err
	}
	if len(strings.TrimSpace(stderr)) > 0 {
		return "", fmt.Errorf("cli.search: received error message: %v\n", stderr)
	}
	return stdout, nil
}

// Executes the given command using the local shell.
func (c *CLI) run(name string, args []string) (string, string, error) {
	cmd := exec.Command(name, args...)
	cmd.Env = append(os.Environ(), cmd.Env...)
	log.Printf(" -> %v\n", cmd.String())

	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	err := cmd.Run()
	return string(stdout.Bytes()), string(stderr.Bytes()), err
}
