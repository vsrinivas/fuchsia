// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This library is intended only for use by tests that want to spin up a local
// ssh server to test ssh client code. It uses password authentication rather
// than key-based authentication, making it insecure and inappropriate for
// production use.

package sshutil

import (
	"crypto/rand"
	"crypto/rsa"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"sync"

	"golang.org/x/crypto/ssh"
)

const (
	testServerUser = "testuser"
)

type sshServer struct {
	// The address (IP + port) that the server is running on.
	addr net.Addr

	// The configuration that clients can use to connect to the server.
	clientConfig *ssh.ClientConfig

	// The configuration used by the server when accepting new connections.
	serverConfig *ssh.ServerConfig

	// The server listens on this channel and shuts down when stop() closes it.
	stopping chan struct{}

	// onNewChannel is a callback that gets called when the server receives a
	// new channel.
	onNewChannel func(ssh.NewChannel)

	// onNewChannel is a callback that gets called when the server receives a
	// new out-of-band request.
	onRequest func(*ssh.Request)

	// wg tracks all the current goroutines that are able to serve connections,
	// or launch new goroutines that themselves are able to serve connections.
	wg sync.WaitGroup
}

// start launches the server and sets the server's address. It launches a
// goroutine that listens for new connections until stop() is called.
func (s *sshServer) start() error {
	// We don't care which port the server runs on as long as it doesn't collide
	// with another process. Specifying ":0" gives us any available port.
	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		return err
	}
	s.addr = listener.Addr()

	// This goroutine is capable of launching new server goroutines, so the
	// server can't be considered shut down if this goroutine is still running.
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		defer func() {
			if err := listener.Close(); err != nil {
				log.Panicf("failed to close listener: %v", err)
			}
		}()

		// Use buffered channels so that if this goroutine exits and stops
		// reading from the channels, the listening goroutine doesn't block
		// trying to send on one of the channels and leak.
		tcpConns := make(chan net.Conn, 1)
		listenerErrs := make(chan error, 1)

		for {
			go func() {
				tcpConn, err := listener.Accept()
				if err != nil {
					listenerErrs <- err
					return
				}
				tcpConns <- tcpConn
			}()

			select {
			case <-s.stopping:
				return
			case err := <-listenerErrs:
				log.Panicf("testserver listener error: %v\n", err)
			case tcpConn := <-tcpConns:
				conn, incomingChannels, incomingRequests, err := ssh.NewServerConn(tcpConn, s.serverConfig)
				if err != nil {
					log.Panicf("testserver connection error: %v\n", err)
				}

				s.wg.Add(1)
				go func() {
					defer s.wg.Done()
					s.serveConnection(conn, incomingChannels, incomingRequests)
				}()
			}
		}
	}()

	return nil
}

// stop shuts down the server.
func (s *sshServer) stop() {
	select {
	case <-s.stopping:
		return // Server has already been stopped, no more work to do.
	default:
		close(s.stopping)
	}
	// Block until we know that no new handshakes can occur, and that any
	// existing connections can no longer be served.
	s.wg.Wait()
}

func (s *sshServer) serveConnection(
	conn *ssh.ServerConn,
	incomingChannels <-chan ssh.NewChannel,
	incomingRequests <-chan *ssh.Request,
) {
	// This might err out if the client is closed first, so don't bother
	// checking the return value.
	defer conn.Close()

	for {
		select {
		case <-s.stopping:
			return
		case newChannel, ok := <-incomingChannels:
			if !ok {
				return
			}
			if s.onNewChannel != nil {
				s.onNewChannel(newChannel)
			}
		case req, ok := <-incomingRequests:
			if !ok {
				return
			}
			if s.onRequest != nil {
				s.onRequest(req)
			}
		}
	}
}

// startSSHServer starts an ssh server on localhost, at any available port.
func startSSHServer(onNewChannel func(ssh.NewChannel), onRequest func(*ssh.Request)) (*sshServer, error) {
	serverConfig, clientConfig, err := genSSHConfig()

	server := &sshServer{
		clientConfig: clientConfig,
		serverConfig: serverConfig,
		stopping:     make(chan struct{}),
		onNewChannel: onNewChannel,
		onRequest:    onRequest,
	}
	if err = server.start(); err != nil {
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
func onNewExecChannel(f func(cmd string, stdout io.Writer, stderr io.Writer) int) func(ssh.NewChannel) {
	return func(newChannel ssh.NewChannel) {
		if newChannel.ChannelType() != "session" {
			newChannel.Reject(ssh.UnknownChannelType, "unknown channel type")
			return
		}

		ch, reqs, err := newChannel.Accept()
		if err != nil {
			log.Panicf("error accepting channel: %v", err)
		}

		go func() {
			defer ch.Close()

			req := <-reqs
			switch req.Type {
			case "exec":
				var execMsg struct{ Command string }
				if err := ssh.Unmarshal(req.Payload, &execMsg); err != nil {
					log.Panicf("failed to unmarshal payload: %v", err)
				}
				if err := req.Reply(true, nil); err != nil {
					log.Panicf("failed to send reply: %v", err)
				}

				exitStatus := f(execMsg.Command, ch, ch.Stderr())

				exitMsg := struct {
					ExitStatus uint32
				}{ExitStatus: uint32(exitStatus)}

				if _, err := ch.SendRequest("exit-status", false, ssh.Marshal(&exitMsg)); err != nil {
					log.Panicf("failed to send exit status: %v", err)
				}
			default:
				log.Panicf("unexpected request type: %v", req.Type)
			}
		}()
	}
}
