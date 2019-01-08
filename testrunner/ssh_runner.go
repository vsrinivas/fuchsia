package testrunner

import (
	"context"
	"fmt"
	"io"
	"strings"
	"time"

	"golang.org/x/crypto/ssh"
)

// SSHRunner runs commands over SSH.
type SSHRunner struct {
	Timeout time.Duration
	Session *ssh.Session
}

func (r *SSHRunner) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	// Set a default timeout if none was given.
	if r.Timeout == time.Duration(0) {
		r.Timeout = defaultTimeout
	}

	r.Session.Stdout = stdout
	r.Session.Stderr = stderr
	cmd := strings.Join(command, " ")

	ctx, cancel := context.WithTimeout(ctx, r.Timeout)
	defer cancel()

	if err := r.Session.Start(cmd); err != nil {
		return err
	}

	done := make(chan error)
	go func() {
		done <- r.Session.Wait()
	}()

	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		r.Session.Signal(ssh.SIGKILL)
	}
	return fmt.Errorf("command timed out after %v", r.Timeout)
}
