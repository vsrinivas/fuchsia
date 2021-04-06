// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This library is intended only for use by tests that want to spin up a local
// ssh server to test ssh client code. It uses password authentication rather
// than key-based authentication, making it insecure and inappropriate for
// production use.

package sshutil

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"errors"
	"fmt"
	"io"
	"log"
	"net"

	"golang.org/x/crypto/ssh"
	"golang.org/x/sync/errgroup"
)

const (
	testServerUser = "testuser"
)

type sshServer struct {
	// The server's listener.
	listener net.Listener

	// The configuration that clients can use to connect to the server.
	clientConfig *ssh.ClientConfig

	// The configuration used by the server when accepting new connections.
	serverConfig *ssh.ServerConfig

	// onNewChannel is a callback that gets called when the server receives a
	// new channel.
	onNewChannel func(ssh.NewChannel)

	// onNewChannel is a callback that gets called when the server receives a
	// new out-of-band request.
	onRequest func(ssh.Channel, *ssh.Request)

	// g tracks all the current goroutines that are able to serve connections,
	// or launch new goroutines that themselves are able to serve connections.
	g *errgroup.Group
}

// start launches the server and sets the server's address. It launches a
// goroutine that listens for new connections until stop() is called.
func (s *sshServer) start(ctx context.Context) error {
	// We don't care which port the server runs on as long as it doesn't collide
	// with another process. Specifying ":0" gives us any available port.
	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		return err
	}
	s.listener = listener

	s.g, ctx = errgroup.WithContext(ctx)

	// This goroutine is capable of launching new server goroutines, so the
	// server can't be considered shut down if this goroutine is still running.
	s.g.Go(func() error {
		for {
			tcpConn, err := listener.Accept()
			if err != nil {
				return err
			}

			conn, incomingChannels, incomingRequests, err := ssh.NewServerConn(tcpConn, s.serverConfig)
			if err != nil {
				return err
			}

			// Inner group for this connection.
			g, ctx := errgroup.WithContext(ctx)
			g.Go(func() error {
				return s.serveRequests(ctx, nil, incomingRequests)
			})

			g.Go(func() error {
				for {
					select {
					case <-ctx.Done():
						return ctx.Err()
					case newChannel, ok := <-incomingChannels:
						if !ok {
							return nil
						}
						if fn := s.onNewChannel; fn != nil {
							fn(newChannel)
						}
						g.Go(func() error {
							ch, incomingRequests, err := newChannel.Accept()
							if err != nil {
								return err
							}
							defer func() {
								if err := ch.Close(); err != nil && !errors.Is(err, io.EOF) {
									log.Printf("ch.Close() = %s", err)
								}
							}()
							return s.serveRequests(ctx, ch, incomingRequests)
						})
					}
				}
			})

			s.g.Go(func() error {
				// This might err out if the client is closed first, so don't bother
				// checking the return value.
				defer func() {
					_ = conn.Close()
				}()

				return g.Wait()
			})
		}
	})

	return nil
}

// stop shuts down the server.
func (s *sshServer) stop() error {
	if err := s.listener.Close(); err != nil && !errors.Is(err, net.ErrClosed) {
		log.Printf("listener.Close() = %s", err)
	}
	// Block until we know that no new handshakes can occur, and that any
	// existing connections can no longer be served.
	if err := s.g.Wait(); !errors.Is(err, net.ErrClosed) {
		return err
	}
	return nil
}

func (s *sshServer) serveRequests(ctx context.Context, ch ssh.Channel, incomingRequests <-chan *ssh.Request) error {
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case req, ok := <-incomingRequests:
			if !ok {
				return nil
			}
			if fn := s.onRequest; fn != nil {
				fn(ch, req)
			}
		}
	}
}

// startSSHServer starts an ssh server on localhost, at any available port.
func startSSHServer(ctx context.Context, onNewChannel func(ssh.NewChannel), onRequest func(ssh.Channel, *ssh.Request)) (*sshServer, error) {
	serverConfig, clientConfig, err := genSSHConfig()

	server := &sshServer{
		clientConfig: clientConfig,
		serverConfig: serverConfig,
		onNewChannel: onNewChannel,
		onRequest:    onRequest,
	}
	if err = server.start(ctx); err != nil {
		return nil, err
	}
	return server, nil
}

func genSSHConfig() (*ssh.ServerConfig, *ssh.ClientConfig, error) {
	clientPassword, err := genPassword(40)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to generate password: %w", err)
	}
	serverConfig := &ssh.ServerConfig{
		MaxAuthTries: 1,
		PasswordCallback: func(metadata ssh.ConnMetadata, password []byte) (*ssh.Permissions, error) {
			if metadata.User() != testServerUser || string(password) != clientPassword {
				return nil, errors.New("invalid user/password combination")
			}
			return nil, nil
		},
	}

	serverKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return nil, nil, fmt.Errorf("error generating keypair: %w", err)
	}
	signer, err := ssh.NewSignerFromKey(serverKey)
	if err != nil {
		return nil, nil, err
	}
	serverConfig.AddHostKey(signer)

	clientConfig := &ssh.ClientConfig{
		User:            testServerUser,
		Auth:            []ssh.AuthMethod{ssh.Password(clientPassword)},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}

	return serverConfig, clientConfig, nil
}

func genPassword(length int) (string, error) {
	buf := make([]byte, length)
	if _, err := rand.Read(buf); err != nil {
		return "", err
	}
	return fmt.Sprintf("%x", buf), nil
}

// onChannelExec is a helper method for creating a sshServer.onNewChannel which
// will call a callback if the new channel request is a session with a single
// request to execute a command. Any other channel or request type will result
// in a panic.
func onExecRequest(f func(cmd string, stdout io.Writer, stderr io.Writer) int) func(ssh.Channel, *ssh.Request) {
	return func(ch ssh.Channel, req *ssh.Request) {
		switch req.Type {
		case "exec":
			var execMsg struct{ Command string }
			if err := ssh.Unmarshal(req.Payload, &execMsg); err != nil {
				log.Panicf("failed to unmarshal payload: %s", err)
			}
			if err := req.Reply(true, nil); err != nil {
				log.Panicf("failed to send reply: %s", err)
			}

			exitStatus := f(execMsg.Command, ch, ch.Stderr())

			exitMsg := struct {
				ExitStatus uint32
			}{ExitStatus: uint32(exitStatus)}

			if _, err := ch.SendRequest("exit-status", false, ssh.Marshal(&exitMsg)); err != nil {
				log.Panicf("failed to send exit status: %s", err)
			}

			if err := ch.Close(); err != nil {
				log.Printf("failed to close channel: %s", err)
			}
		case keepaliveFuchsia:
			// Ignore.
		default:
			log.Panicf("unexpected request type: %s", req.Type)
		}
	}
}
