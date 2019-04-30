package packages

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	tuf_data "github.com/flynn/go-tuf/data"
)

type Server struct {
	Dir    string
	URL    string
	Hash   string
	server *http.Server
}

func newServer(dir string, localHostname string) (*Server, error) {
	listener, err := net.Listen("tcp", ":0")
	if err != nil {
		return nil, err
	}

	port := listener.Addr().(*net.TCPAddr).Port
	log.Printf("Serving %s on :%d", dir, port)

	configURL, configHash, err := writeConfig(dir, localHostname, port)
	if err != nil {
		listener.Close()
		return nil, err
	}

	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.Dir(dir)))

	server := &http.Server{
		Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			lw := &loggingWriter{w, 0}
			mux.ServeHTTP(lw, r)
			log.Printf("%s [pm serve] %d %s\n",
				time.Now().Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
		}),
	}

	go func() {
		if err := server.Serve(listener); err != http.ErrServerClosed {
			log.Fatalf("failed to shutdown server: %s", err)
		}
	}()

	return &Server{
		Dir:    dir,
		URL:    configURL,
		Hash:   configHash,
		server: server,
	}, err
}

func (s *Server) Shutdown(ctx context.Context) {
	s.server.Shutdown(ctx)
}

type loggingWriter struct {
	http.ResponseWriter
	status int
}

func (lw *loggingWriter) WriteHeader(status int) {
	lw.status = status
	lw.ResponseWriter.WriteHeader(status)
}

// writeConfig writes the source config to the repository.
func writeConfig(dir string, localHostname string, port int) (configURL string, configHash string, err error) {
	type keyConfig struct {
		Type  string
		Value string
	}

	type statusConfig struct {
		Enabled bool
	}

	type sourceConfig struct {
		ID           string       `json:"id"`
		RepoURL      string       `json:"repoUrl"`
		BlobRepoURL  string       `json:"blobRepoUrl"`
		RootKeys     []keyConfig  `json:"rootKeys"`
		StatusConfig statusConfig `json:"statusConfig"`
	}

	f, err := os.Open(filepath.Join(dir, "root.json"))
	if err != nil {
		return "", "", err
	}
	defer f.Close()

	var signed tuf_data.Signed
	if err := json.NewDecoder(f).Decode(&signed); err != nil {
		return "", "", err
	}

	var root tuf_data.Root
	if err := json.Unmarshal(signed.Signed, &root); err != nil {
		return "", "", err
	}

	var rootKeys []keyConfig
	for _, keyID := range root.Roles["root"].KeyIDs {
		key := root.Keys[keyID]

		rootKeys = append(rootKeys, keyConfig{
			Type:  key.Type,
			Value: key.Value.Public.String(),
		})
	}

	hostname := strings.SplitN(localHostname, "%", 2)[0]
	repoURL := fmt.Sprintf("http://[%s]:%d", hostname, port)
	configURL = fmt.Sprintf("%s/system_ota_tests/config.json", repoURL)

	config, err := json.Marshal(&sourceConfig{
		ID:          "system_ota_tests",
		RepoURL:     repoURL,
		BlobRepoURL: fmt.Sprintf("%s/blobs", repoURL),
		RootKeys:    rootKeys,
		StatusConfig: statusConfig{
			Enabled: true,
		},
	})
	if err != nil {
		return "", "", err
	}
	h := sha256.Sum256(config)
	configHash = hex.EncodeToString(h[:])

	configDir := filepath.Join(dir, "system_ota_tests")
	if err := os.MkdirAll(configDir, 0755); err != nil {
		return "", "", err
	}

	configPath := filepath.Join(configDir, "config.json")
	log.Printf("writing %q", configPath)
	if err := ioutil.WriteFile(configPath, config, 0644); err != nil {
		return "", "", err
	}

	return configURL, configHash, nil
}
