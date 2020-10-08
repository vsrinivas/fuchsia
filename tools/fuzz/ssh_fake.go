// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path"
	"strings"
	"time"

	"github.com/golang/glog"
	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

// Spins up a local, in-memory SSH server for testing, blocking until the
// server is initialized.  Any startup errors will be returned from this
// function.  Any runtime errors will be passed back on the error channel.  The
// SSH server itself is real, but its subsystems (exec, sftp) are fakes for
// testing.
func startLocalSSHServer() (*SSHConnector, <-chan error, *fakeSftp, error) {
	connCh := make(chan *SSHConnector, 1)
	errCh := make(chan error, 1)

	fakeFs := &fakeSftp{}

	go serveSSH(connCh, errCh, fakeFs)

	// We expect either a startup error or nil to indicate success
	if err := <-errCh; err != nil {
		return nil, nil, nil, fmt.Errorf("error starting fake SSH server: %s", err)
	}

	return <-connCh, errCh, fakeFs, nil
}

func serveSSH(connCh chan<- *SSHConnector, errCh chan<- error, fakeFs *fakeSftp) {
	// Note: Even though startLocalSSHServer is going to block on these
	// initialization steps , they are inside this goroutine because that is
	// the scope where the deferred cleanup belongs

	defer close(errCh)

	dir, err := ioutil.TempDir("", "clusterfuchsia_test")
	if err != nil {
		errCh <- fmt.Errorf("error creating temp dir: %s", err)
		return
	}

	defer os.RemoveAll(dir)

	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		errCh <- fmt.Errorf("error during Listen: %s", err)
		return
	}

	defer listener.Close()

	clientKey, err := createSSHKey()
	if err != nil {
		errCh <- fmt.Errorf("error creating client key: %s", err)
		return
	}

	pemFile := path.Join(dir, "ssh.key")
	if err := writeSSHPrivateKeyFile(clientKey, pemFile); err != nil {
		errCh <- fmt.Errorf("error writing ssh private key: %s", err)
		return
	}

	serverKey, err := createSSHKey()
	if err != nil {
		errCh <- fmt.Errorf("error creating server key: %s", err)
		return
	}

	signer, err := ssh.NewSignerFromKey(serverKey)
	if err != nil {
		errCh <- fmt.Errorf("error configuring server key: %s", err)
		return
	}

	config := &ssh.ServerConfig{
		PublicKeyCallback: func(c ssh.ConnMetadata, pubKey ssh.PublicKey) (*ssh.Permissions,
			error) {
			// TODO(fxbug.dev/45424): actually verify the key
			return &ssh.Permissions{}, nil
		},
	}

	config.AddHostKey(signer)

	// Return a suitable connector for the test to use
	addr := listener.Addr().(*net.TCPAddr)
	connCh <- &SSHConnector{Host: "127.0.0.1", Port: addr.Port, Key: pemFile}

	// Indicate initialization is complete
	errCh <- nil

	conn, err := listener.Accept()
	if err != nil {
		errCh <- fmt.Errorf("error during Accept: %s", err)
		return
	}

	defer conn.Close()

	server, newChannels, reqCh, err := ssh.NewServerConn(conn, config)
	if err != nil {
		errCh <- fmt.Errorf("error during ssh handshake: %s", err)
		return
	}

	// We don't expect to get any global requests, and this channel must be serviced
	go ssh.DiscardRequests(reqCh)

	for newChannel := range newChannels {
		if newChannel.ChannelType() != "session" {
			newChannel.Reject(ssh.UnknownChannelType, "unknown channel type")
			continue
		}

		channel, requests, err := newChannel.Accept()
		if err != nil {
			errCh <- fmt.Errorf("could not accept channel: %s", err)
			return
		}

		go handleSSHSession(server, requests, channel, errCh, fakeFs)
	}
}

func handleSSHSession(server *ssh.ServerConn, requests <-chan *ssh.Request,
	channel ssh.Channel, errCh chan<- error, fakeFs *fakeSftp) {

	defer server.Close() // TODO(fxbug.dev/47316): is this necessary / the right scope for this?

	for req := range requests {
		switch req.Type {
		case "exec":
			cmdlen := req.Payload[3]
			cmdline := string(req.Payload[4 : 4+cmdlen])

			req.Reply(true, nil)

			output, exitCode := fakeExec(cmdline)

			_, err := channel.Write([]byte(output))
			if err != nil {
				errCh <- fmt.Errorf("write fail: %s", err)
				return
			}

			if err := channel.CloseWrite(); err != nil { // sends EOF
				errCh <- fmt.Errorf("error during closewrite: %s", err)
				return
			}

			if _, err := channel.SendRequest("exit-status", false,
				[]byte{0, 0, 0, exitCode}); err != nil {
				errCh <- fmt.Errorf("error during exit-status: %s", err)
				return
			}

			if err := channel.Close(); err != nil {
				errCh <- fmt.Errorf("error during Close: %s", err)
				return
			}

		case "subsystem":
			cmdlen := req.Payload[3]
			cmd := string(req.Payload[4 : 4+cmdlen])

			if cmd != "sftp" {
				req.Reply(false, nil)
				continue
			}

			req.Reply(true, nil)

			root := makeSftpHandler(fakeFs)
			sftpServer := sftp.NewRequestServer(channel, root)
			if err := sftpServer.Serve(); err != nil && err != io.EOF {
				errCh <- fmt.Errorf("error during sftp serve: %s", err)
				return
			}
		default:
			req.Reply(false, nil)
		}
	}
}

func fakeExec(cmdline string) (stdout string, exitCode byte) {
	args := strings.Split(cmdline, " ")

	switch args[0] {
	case "echo":
		return strings.Join(args[1:], " ") + "\n", 0
	default:
		return "", 127
	}
}

// The following is based on pkg/sftp/request-example.go:

type fakeFile struct {
	name    string
	content string
	isDir   bool
}

// implement os.FileInfo
func (f *fakeFile) Name() string { return path.Base(f.name) }
func (f *fakeFile) Size() int64  { return int64(len(f.content)) }
func (f *fakeFile) Mode() os.FileMode {
	mode := os.FileMode(0644)
	if f.IsDir() {
		mode |= os.ModeDir
	}
	return mode
}
func (f *fakeFile) ModTime() time.Time { return time.Now() }
func (f *fakeFile) IsDir() bool        { return f.isDir }
func (f *fakeFile) Sys() interface{}   { return nil }

func (f *fakeFile) WriteAt(p []byte, off int64) (n int, err error) {
	// Note: this ignores offset, assuming sequential writes
	f.content = f.content + string(p)
	return len(p), nil
}

type fakeSftp struct {
	files []*fakeFile
}

type listerat []os.FileInfo

func (f listerat) ListAt(ls []os.FileInfo, offset int64) (int, error) {
	var n int
	if offset >= int64(len(f)) {
		return 0, io.EOF
	}
	n = copy(ls, f[offset:])
	if n < len(ls) {
		return n, io.EOF
	}
	return n, nil
}

func (s *fakeSftp) getFile(path string) (*fakeFile, error) {
	for _, f := range s.files {
		if f.name == path {
			return f, nil
		}
	}

	return nil, os.ErrNotExist
}

func (s *fakeSftp) Fileread(r *sftp.Request) (io.ReaderAt, error) {
	glog.Infof("sftp read: %v", r)

	f, err := s.getFile(r.Filepath)
	if err != nil {
		return nil, err
	}
	return strings.NewReader(f.content), nil
}

func (s *fakeSftp) Filewrite(r *sftp.Request) (io.WriterAt, error) {
	glog.Infof("sftp write: %v", r)

	// Enforce that directory must exist
	enclosingDir := path.Dir(r.Filepath)
	if enclosingDir != "." {
		if dir, err := s.getFile(enclosingDir); err != nil || !dir.isDir {
			glog.Errorf("directory doesn't exist: %q", enclosingDir)
			return nil, os.ErrNotExist
		}
	}

	// Note: this doesn't handle pre-existing files
	f := &fakeFile{name: r.Filepath}
	s.files = append(s.files, f)

	return f, nil
}

func (s *fakeSftp) Filecmd(r *sftp.Request) error {
	glog.Infof("sftp cmd: %v", r)

	switch r.Method {
	case "Mkdir":
		// Make sure it doesn't already exist
		if _, err := s.getFile(r.Filepath); err == nil {
			return os.ErrExist
		}

		// Make sure its parent dir exists
		if f, err := s.getFile(path.Dir(r.Filepath)); err != nil || !f.isDir {
			return os.ErrNotExist
		}

		f := &fakeFile{name: r.Filepath, isDir: true}
		s.files = append(s.files, f)

		return nil
	default:
		return fmt.Errorf("unsupported: %v", r)
	}
}

func (s *fakeSftp) Filelist(r *sftp.Request) (sftp.ListerAt, error) {
	glog.Infof("sftp list: %v", r)

	f, err := s.getFile(r.Filepath)
	if err != nil {
		return nil, err
	}

	switch r.Method {
	case "Stat":
		return listerat([]os.FileInfo{f}), nil
	case "List":
		if f.isDir {
			var children []os.FileInfo
			for _, f2 := range s.files {
				// Only list files/dirs in the immediate directory
				if dir := path.Dir(f2.name); dir == f.name {
					children = append(children, f2)
				}
			}
			return listerat(children), nil
		}

		return listerat([]os.FileInfo{f}), nil
	default:
		return nil, fmt.Errorf("unsupported: %v", r)
	}
}

func makeSftpHandler(handler *fakeSftp) sftp.Handlers {
	return sftp.Handlers{FileGet: handler, FilePut: handler, FileCmd: handler, FileList: handler}
}
