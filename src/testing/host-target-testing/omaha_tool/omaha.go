// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package omaha_tool

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type OmahaToolArgs struct {
	toolPath       string
	privateKeyId   string
	privateKeyPath string
	AppId          string
	localHostname  string
	merkle         string
	packagePath    string
}

// Matches ResponseAndMetadata within mock-omaha-server/src/lib.rs.
type responseAndMetadata struct {
	Response        string `json:"response,omitempty"`
	Merkle          string `json:"merkle,omitempty"`
	CheckAssertion  string `json:"check_assertion,omitempty"`
	Version         string `json:"version,omitempty"`
	CohortAssertion string `json:"cohort_assertion,omitempty"`
	Codebase        string `json:"codebase,omitempty"`
	PackagePath     string `json:"package_path,omitempty"`
}

// Service this update package URL as the current update package.
func (a *OmahaToolArgs) SetUpdatePkgURL(ctx context.Context, updatePkgURL string) error {
	// Expected input format: fuchsia-pkg://fuchsia.com/update?hash=abcdef
	u, err := url.Parse(updatePkgURL)
	if err != nil {
		return fmt.Errorf("invalid update package URL %q: %w", updatePkgURL, err)
	}
	if u.Scheme != "fuchsia-pkg" {
		return fmt.Errorf("scheme must be fuchsia-pkg, not %q", u.Scheme)
	}
	if u.Host == "" {
		return fmt.Errorf("update package URL's host must not be empty")
	}
	// The merkle field is the URL query param 'hash'.
	q := u.Query()
	a.merkle = q.Get("hash")
	a.packagePath = strings.TrimPrefix(u.Path, "/")
	return nil
}

type OmahaTool struct {
	Args      OmahaToolArgs
	serverURL string
	cmd       *exec.Cmd
}

func NewOmahaServer(ctx context.Context, args OmahaToolArgs, stdout io.Writer, stderr io.Writer) (*OmahaTool, error) {
	l := logger.NewLogger(
		logger.DebugLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"omaha-server: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	privateKeyPath, err := exec.LookPath(args.privateKeyPath)
	if err != nil {
		return nil, err
	}

	toolPath, err := exec.LookPath(args.toolPath)
	if err != nil {
		return nil, err
	}

	toolArgs := []string{
		"--key-id", args.privateKeyId,
		"--key-path", privateKeyPath,
		"--listen-on", "::1",
	}
	logger.Infof(ctx, "running: %s %q", toolPath, toolArgs)
	cmd := exec.CommandContext(ctx, toolPath, toolArgs...)

	if stderr != nil {
		cmd.Stderr = stderr
	} else {
		cmd.Stderr = os.Stderr
	}

	var port string

	stdoutPipe, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}

	err = cmd.Start()
	if err != nil {
		return nil, err
	}

	lineCh := make(chan string)
	go func() {
		scanner := bufio.NewScanner(stdoutPipe)
		if !scanner.Scan() {
			return
		}
		line := scanner.Text()
		lineCh <- line
		_, err := io.WriteString(stdout, line)
		if err != nil {
			logger.Errorf(ctx, "error: %s", err)
			return
		}

		for scanner.Scan() {
			_, err := io.WriteString(stdout, scanner.Text())
			if err != nil {
				logger.Errorf(ctx, "error: %s", err)
				return
			}
		}
	}()

	select {
	case line, ok := <-lineCh:
		if !ok {
			return nil, errors.New("Channel closed without receiving any lines")
		}

		// parse line
		words := strings.Fields(line)
		_, parsedPort, err := net.SplitHostPort(words[2])
		if err != nil {
			return nil, err
		}
		port = parsedPort

	case <-time.After(10 * time.Second):
		// handle timeout
		return nil, fmt.Errorf("Timed out waiting for first stdout from mock-omaha-server")
	}

	logger.Infof(ctx, "Serving Omaha from %s", port)
	hostname := strings.ReplaceAll(args.localHostname, "%", "%25")

	var serverURL string
	if strings.Contains(hostname, ":") {
		// This is an IPv6 address, use brackets for an IPv6 literal
		serverURL = fmt.Sprintf("http://[%s]:%s", hostname, port)
	} else {
		serverURL = fmt.Sprintf("http://%s:%s", hostname, port)
	}
	o := OmahaTool{
		Args:      args,
		serverURL: serverURL,
		cmd:       cmd,
	}

	return &o, nil
}

// Returns nil if the process has already stopped.
func (o *OmahaTool) Shutdown() error {
	if process := o.cmd.Process; process != nil {
		return o.cmd.Process.Kill()
	}
	return nil
}

func (o *OmahaTool) URL() string {
	return o.serverURL
}

func (o *OmahaTool) SetPkgURL(ctx context.Context, updatePkgURL string) error {
	newArgs := o.Args
	if err := newArgs.SetUpdatePkgURL(ctx, updatePkgURL); err != nil {
		return err
	}
	o.Args = newArgs

	responsesByAppID := map[string]responseAndMetadata{
		o.Args.AppId: {
			Response:       "Update",
			Merkle:         o.Args.merkle,
			CheckAssertion: "UpdatesEnabled",
			Version:        "0.1.2.3",
			Codebase:       "fuchsia-pkg://fuchsia.com",
			PackagePath:    o.Args.packagePath,
		},
	}
	req, err := json.Marshal(responsesByAppID)
	if err != nil {
		return err
	}

	_, err = http.Post(o.URL()+"/set_responses_by_appid", "application/json", bytes.NewBuffer(req))
	return err
}
