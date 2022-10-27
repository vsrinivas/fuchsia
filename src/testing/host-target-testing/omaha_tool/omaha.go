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
	ToolPath       string
	PrivateKeyId   string
	PrivateKeyPath string
	AppId          string
	LocalHostname  string
	RequireCup     bool
}

type responseAndMetadata struct {
	// Note: Keep this struct up-to-date with ResponseAndMetadata within
	// mock-omaha-server/src/lib.rs
	Response        string `json:"response,omitempty"`
	Merkle          string `json:"merkle,omitempty"`
	CheckAssertion  string `json:"check_assertion,omitempty"`
	Version         string `json:"version,omitempty"`
	CohortAssertion string `json:"cohort_assertion,omitempty"`
	Codebase        string `json:"codebase,omitempty"`
	PackagePath     string `json:"package_path,omitempty"`
}

type OmahaTool struct {
	Args      OmahaToolArgs
	serverURL string
	cmd       *exec.Cmd
	stdoutBuf *bytes.Buffer
}

func NewOmahaServer(ctx context.Context, args OmahaToolArgs, providedStdout io.Writer, providedStderr io.Writer) (*OmahaTool, error) {
	l := logger.NewLogger(
		logger.DebugLevel,
		color.NewColor(color.ColorAuto),
		os.Stdout,
		os.Stderr,
		"omaha-server: ")
	l.SetFlags(logger.Ldate | logger.Ltime | logger.LUTC | logger.Lshortfile)
	ctx = logger.WithLogger(ctx, l)

	privateKeyPath, err := exec.LookPath(args.PrivateKeyPath)
	if err != nil {
		return nil, err
	}

	toolPath, err := exec.LookPath(args.ToolPath)
	if err != nil {
		return nil, err
	}

	toolArgs := []string{
		"--key-id", args.PrivateKeyId,
		"--key-path", privateKeyPath,
	}
	if args.RequireCup {
		toolArgs = append(toolArgs, "--require-cup")
	}
	logger.Infof(ctx, "running: %s %q", toolPath, toolArgs)
	cmd := exec.CommandContext(ctx, toolPath, toolArgs...)

	if providedStderr != nil {
		cmd.Stderr = providedStderr
	} else {
		cmd.Stderr = os.Stderr
	}

	var serverStdout io.Writer
	if providedStdout == nil {
		serverStdout = os.Stdout
	} else {
		serverStdout = providedStdout
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
		_, err := io.WriteString(serverStdout, line)
		if err != nil {
			logger.Errorf(ctx, "error: %s", err)
			return
		}

		for scanner.Scan() {
			_, err := io.WriteString(serverStdout, scanner.Text())
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
	hostname := strings.ReplaceAll(args.LocalHostname, "%", "%25")

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
func (o *OmahaTool) Shutdown(ctx context.Context) error {
	process := o.cmd.Process
	if process == nil {
		return nil
	}
	if err := process.Kill(); err != nil {
		return err
	}

	ch := make(chan error)
	go func() {
		ch <- o.cmd.Wait()
	}()
	select {
	case err := <-ch:
		// If the process has been killed, ignore the error and report a
		// successful shutdown.
		if _, ok := err.(*exec.ExitError); ok {
			return nil
		}
		return err
	case <-ctx.Done():
		return ctx.Err()
	}
}

func (o *OmahaTool) URL() string {
	return o.serverURL
}

func (o *OmahaTool) SetPkgURL(ctx context.Context, updatePkgURL string) error {
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

	responsesByAppID := map[string]responseAndMetadata{
		o.Args.AppId: {
			Response:       "Update",
			Merkle:         u.Query().Get("hash"),
			CheckAssertion: "UpdatesEnabled",
			Codebase:       "fuchsia-pkg://" + u.Host + "/",
			PackagePath:    strings.TrimPrefix(u.Path, "/"),
		},
	}
	req, err := json.Marshal(responsesByAppID)
	if err != nil {
		return err
	}

	_, err = http.Post(o.URL()+"/set_responses_by_appid", "application/json", bytes.NewBuffer(req))
	return err
}
