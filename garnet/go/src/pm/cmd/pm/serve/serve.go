// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"compress/gzip"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/repo"
	"fuchsia.googlesource.com/sse"
)

const js = `
async function main() {
	let $ = (s) => document.querySelector(s);
	$("#icon").src = $('link[rel="icon"]').href;

	let res = await fetch("/targets.json");
	let manifest = await res.json();
	let targets = manifest.signed.targets;

	$("#version").innerText = manifest.signed.version;
	$("#expires").innerText = manifest.signed.expires;

	let $c = (e) => document.createElement(e);

	let table = $("#package-table > tbody");
	for (let pkg in targets) {
		let row = $c("tr");
		let pkgcol = $c("td");
		let merklecol = $c("td");
		merklecol.classList.add('merkle');
		row.appendChild(pkgcol);
		row.appendChild(merklecol);
		let a = $c("a");
		a.href = "fuchsia-pkg://" + window.location.host + pkg;
		a.innerText = pkg.slice(1);
		pkgcol.appendChild(a);
		merklecol.innerText = targets[pkg].custom.merkle;
		table.appendChild(row);
	}

	$("#spinner").classList.remove("is-active");
}
main();
`

const indexHTML = `
<!doctype html>
<link rel="stylesheet" defer href="https://code.getmdl.io/1.3.0/material.indigo-pink.min.css">
<link rel="icon" href="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAYAAABzenr0AAAAAXNSR0IArs4c6QAABuxJREFUWAnll3+QlWUVx7/nve9eVGRLSRKQ/sgfyezdxSQbScdhKjPJEhssaiomc2zEiRF/QBAtlxWYkUisyRDHrIkxG20aMx2GP5wa+iU5Gay7uYj0QyCEUcEFXHbvfe/T5zz3Xve2u8DyXzOdmec+z/s853zPec45z3meK/2/k43WAUGrx0uVFtoFtPdI1kQ7KoV9UvqSVOoxFQdGi1fnO6kBQffMRNENKHID9jD+O/0B+hJzY+kn0zBK76bvkrLHMOSfdQUn649rQNCyqexsEaB9tMdQuvVEOwwq4pX0Wqn8GfhfkA7db1qH7IlpRAMAm4vYFwACZPlvGiFCW3GCLDlHoTJGSe6w+vKv2Y7Fh+s8yJ7GeD5tBsZ807RyV31tpH6YAUErbsW1l0tjbjctOehCYeqqiUozDArXyzRVwd0dcjLrZ3m/gv05eim/+xn7y0OEhlUVr6JbLCWLTO3dPjcS/ZcBKP88rp4ljb/FtKA/gKzWjvkoWiJLJ7NrMLzVxUIV03Jo9HG2hbVF1tm+1Rcw4hK6lYTm66Zle31uKNWRnPliFu+lzSPWh8KM+07X0cPrUTxPIavKuaJK6RjM+zHqGCrexcK5SjgQlTK6WVc4rErlNutavtGF2NQ1/H5R6r7J9EQNyFeqlHiHcu/dXWui8hsfz+lo7wZO2jwU1oF3ouQOPH8pAgX1nTVNpXyBfLhKofQgTnnbkWjjlCQ/Cq0rbnRscmgz3b+lFkI4nKIHgto/hvLZKP+Gs4RCcYFyTd+rKk+ZyH6sUtPd1rP0jeEQ1RmS83KF5BEMmhrDFPSGkspHbHvxZTY4Aa4fSkfw7lpqxyBFD6B8DvH7qU+HwsopuLc9ut1dq/LDenH5106k3OWss/gccp8iT8h69pXkxuOQjrimInVDJOK4T/h3IyVYdy4T46Qezq5TdhMxpegAUil3q//tOxjVsq3Kcbxf6/z2P5C5jfVyzImQzMYzhRr/r4H56FBZ94Bn6iueIGH6BrYcbqgmnUcnrLUda94540OFR/q27o7NqmTPxoRM0jFs5Poq31t4wM5iw2c0yrkBnv09cbL/wPvY+EVxw6H8pkLTpkbm0Y/tCcIBDEc2hCtcrloVwxGcQ+keJAyw6/h8NU7ZwHkk0enODu1S11KP3alTKHcSAtfuslPCzCKZHOmQlKdkDxIGBGKU1DIzxT2uPBrQa7JRxX4QrjbK5XvBpTBEOkP9zZ7NTuCVh4WA2FS41ZysdpbjRzPc0ZL4dSo/lYyktvquj+lPCylakX7FZnc0QnkInqZNiZPB9hC3vniOpfersNTP76mTWauS1LFddqfa7rkyTFu1QZfaRM3U642AMHkChg/EyZDtjgIuaOl45cYMO7eNwscfhznxbjCH1xY2dZ2S/C3K0l/qTftDmFb8cF0Wjv5tfFzo5di644vmyVpNxxHhrtB2Vy08dZET96G1+HHkr45HuXpvPIfEZ6vf8b6glCdPh5aOaY6UmFbvoyf2sR6QI+kjlGCuYTIgaWqjRn3HGUdDYcZCTpBfaDzXEk+B8JTKeY/5KmXH1tH3xqNpuXMo09/3uhODxO5xtc3i4rgdJs7FijuVS9cO3gXlh5TTYttW5BiNTKGFipqzy7D6qaoSO6jKwBXWtfKlukRoW8GdY08SnjM57oGCdWXdAA/WTxB+gMfD1nhuX9dG5fJzAXFPeFl2oAcIy2Y1N+/VQE9Z2aSxPA8oZE1f4UY8jdR5lc0vjwqz0nrrKs6P44YfQvQoeFzPUFZaHbOEW9BfGWvQsgRvNNtvi2UlR25G+c+icn8P+C2XpD8gaNt0pLdTA5NeUKYu3ix/5Oa8FXlipjNpVTLbWR8O6V8BDG5Umj4UDXAGjOii+wVtXRCx6eTafFFfZmcLgeYBkqsJ2VjGF9D8LTCZkmsRTJoEX8MRC4RjRJrORn1DvvhsDEEjGx5YwPclUt9C071v+VoofGsKx+hLSH2a+F2Mtc30WCTiYwdozwP6MAYcAvj3SIDLi8kqX+Wa/rljOIW2jpvp1iObgsGNGaYPMyAyqogyzSH43yUnfudzTrEyfrA4kcEEWl4Zr+I02/dOcgZqd2txE7lzTTWBCZLCJnhfxlMt9F5XrJpTA4/zzpg7ogFVZfEev5sxdT19VLroedPnAByZ8NzZrJytQs7N2Ey+nF81AkfFmxHzPZc8oUOpixB+0rYv23tcA+pqeFReDcJsWjNu/hfzu0DYz3c/aNT8cB6NKzyhbFee4ShvDIVl5xOy++CdhaIUl9eMyAbwAoWucieh2eM6TmqAMzkFLX0v/xUKiFxYU+oXzmu03SxzRA/+beg/IardZUqNRyt543/bLGyx7e1/jYD/Kz//AbIPhtw1ZC9VAAAAAElFTkSuQmCC">
<script defer src="https://code.getmdl.io/1.3.0/material.min.js"></script>
<title>Package Repository</title>
<style>
body {
	margin: 10px;
}
#package-table .merkle {
	font-family: monospace;
}
#icon {
	height: 32px;
	width: 32px;
	margin: 12px;
}
h1 {
	color: #666;
}
</style>
<header><h1><img id=icon></img>Package Repository</h1></header>
<div id=metadata>
<div>Version: <span id=version></span></div>
<div>Expires: <span id=expires></span></div>
</div>
<div id=spinner class="mdl-spinner mdl-js-spinner is-active"></div>
<table id=package-table class=mdl-data-table>
<thead>
<tr>
<th>Package</th>
<th>Merkle</th>
</tr>
</thead>
<tbody>
</tbody>
</table>
<script async src=js></script>
`

type gzipWriter struct {
	http.ResponseWriter
	*gzip.Writer
}

func (w *gzipWriter) Header() http.Header {
	return w.ResponseWriter.Header()
}

func (w *gzipWriter) Write(b []byte) (int, error) {
	return w.Writer.Write(b)
}

func (w *gzipWriter) Flush() {
	if err := w.Writer.Flush(); err != nil {
		panic(err)
	}
	if f, ok := w.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	} else {
		log.Fatal("server misconfigured, can not flush")
	}
}

type loggingWriter struct {
	http.ResponseWriter
	status int
}

func (lw *loggingWriter) WriteHeader(status int) {
	lw.status = status
	lw.ResponseWriter.WriteHeader(status)
}

func (lw *loggingWriter) Flush() {
	if f, ok := lw.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	} else {
		log.Fatal("server misconfigured, can not flush")
	}
}

var _ http.Flusher = &loggingWriter{}
var _ http.Flusher = &gzipWriter{}

var (
	mu          sync.Mutex
	autoClients = map[http.ResponseWriter]struct{}{}
)

func Run(cfg *build.Config, args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	repoDir := fs.String("d", "", "(deprecated, use -repo) path to the repository")

	config := &repo.Config{}
	config.Vars(fs)

	listen := fs.String("l", ":8083", "HTTP listen address")
	auto := fs.Bool("a", true, "Host auto endpoint for realtime client updates")
	autoRate := fs.Duration("auto-rate", time.Second, "rate at which to poll filesystem if realtime watch is not available")
	quiet := fs.Bool("q", false, "Don't print out information about requests")
	encryptionKey := fs.String("e", "", "Path to a symmetric blob encryption key *UNSAFE*")

	fs.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: %s serve", filepath.Base(os.Args[0]))
		fmt.Fprintln(os.Stderr)
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return err
	}
	config.ApplyDefaults()

	if *repoDir == "" {
		*repoDir = filepath.Join(config.RepoDir, "repository")
	}

	fi, err := os.Stat(*repoDir)
	if err != nil {
		return fmt.Errorf("repository path %q is not valid: %s", *repoDir, err)
	}

	if !fi.IsDir() {
		return fmt.Errorf("repository path %q is not a directory", *repoDir)
	}

	if *auto {
		// TODO(raggi): move to fsnotify
		go func() {
			timestampPath := filepath.Join(*repoDir, "timestamp.json")
			lastUpdateTime := time.Now()
			t := time.NewTicker(*autoRate)
			for range t.C {
				fi, err := os.Stat(timestampPath)
				if err != nil {
					continue
				}

				if fi.ModTime().After(lastUpdateTime) {
					lastUpdateTime = fi.ModTime()
					mu.Lock()
					for w := range autoClients {
						// errors are ignored, as close notifier in the handler
						// ultimately handles cleanup
						sse.Write(w, &sse.Event{
							Event: "timestamp.json",
							Data:  []byte(lastUpdateTime.Format(http.TimeFormat)),
						})
					}
					mu.Unlock()
				}
			}
		}()

		http.HandleFunc("/auto", func(w http.ResponseWriter, r *http.Request) {
			err := sse.Start(w, r)
			if err != nil {
				log.Printf("SSE request failure: %s", err)
				w.WriteHeader(http.StatusInternalServerError)
				return
			}
			mu.Lock()
			autoClients[w] = struct{}{}
			defer func() {
				mu.Lock()
				delete(autoClients, w)
				mu.Unlock()
			}()
			mu.Unlock()
			<-r.Context().Done()
		})
	}

	dirServer := http.FileServer(http.Dir(*repoDir))
	http.Handle("/", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/":
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			w.WriteHeader(200)
			io.WriteString(w, indexHTML)
		case "/js":
			w.Header().Set("Content-Type", "text/javascript; charset=utf-8")
			w.WriteHeader(200)
			io.WriteString(w, js)
		default:
			dirServer.ServeHTTP(w, r)
		}
	}))

	http.Handle("/config.json", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {

		var scheme = "http://"
		if r.TLS != nil {
			scheme = "https://"
		}

		repoUrl := fmt.Sprintf("%s%s", scheme, r.Host)

		var signedKeys struct {
			Signed struct {
				Keys map[string]struct {
					Keytype string
					Keyval  struct {
						Public string
					}
				}
				Roles struct {
					Root struct {
						Keyids []string
					}
					Threshold int
				}
			}
		}
		f, err := os.Open(filepath.Join(*repoDir, "root.json"))
		if err != nil {
			w.WriteHeader(http.StatusInternalServerError)
			log.Printf("root.json missing or unreadable: %s", err)
			return
		}
		defer f.Close()
		if err := json.NewDecoder(f).Decode(&signedKeys); err != nil {
			w.WriteHeader(http.StatusInternalServerError)
			log.Printf("root.json parsing error: %s", err)
			return
		}

		cfg := struct {
			ID          string
			RepoURL     string
			BlobRepoURL string
			RatePeriod  int
			RootKeys    []struct {
				Type  string
				Value string
			}
			StatusConfig struct {
				Enabled bool
			}
			Auto    bool
			BlobKey *struct {
				Data [32]uint8
			}
		}{
			ID:          repoUrl,
			RepoURL:     repoUrl,
			BlobRepoURL: repoUrl + "/blobs",
			RatePeriod:  60,
			StatusConfig: struct {
				Enabled bool
			}{
				Enabled: true,
			},
			Auto: true,
		}

		if *encryptionKey != "" {
			keyBytes, err := ioutil.ReadFile(*encryptionKey)
			if err != nil {
				log.Fatal(err)
			}
			if len(keyBytes) != 32 {
				log.Fatalf("encryption key %s of improper size", *encryptionKey)
			}
			cfg.BlobKey = &struct{ Data [32]uint8 }{}
			copy(cfg.BlobKey.Data[:], keyBytes)
		}

		for _, id := range signedKeys.Signed.Roles.Root.Keyids {
			k := signedKeys.Signed.Keys[id]
			cfg.RootKeys = append(cfg.RootKeys, struct{ Type, Value string }{
				Type:  k.Keytype,
				Value: k.Keyval.Public,
			})
		}
		json.NewEncoder(w).Encode(cfg)
	}))

	if !*quiet {
		fmt.Printf("%s [pm serve] serving %s at http://%s\n",
			time.Now().Format("2006-01-02 15:04:05"), *repoDir, *listen)
	}

	return http.ListenAndServe(*listen, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasPrefix(r.RequestURI, "/blobs") && strings.Contains(r.Header.Get("Accept-Encoding"), "gzip") {
			gw := &gzipWriter{
				w,
				gzip.NewWriter(w),
			}
			defer gw.Close()
			gw.Header().Set("Content-Encoding", "gzip")
			w = gw
		}
		lw := &loggingWriter{w, 0}
		http.DefaultServeMux.ServeHTTP(lw, r)
		if !*quiet {
			fmt.Printf("%s [pm serve] %d %s\n",
				time.Now().Format("2006-01-02 15:04:05"), lw.status, r.RequestURI)
		}
	}))
}
