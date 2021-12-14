// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hyper::{Body, Request, Response, StatusCode};
use std::fs::File;
use std::io::Read as _;
use std::path::Path;

/// Serves up static files from the local fs based on a path relative to $FUCHSIA_DIR.
pub struct Visualizer {
    fuchsia_root: String,
    static_root: String,
}

impl Visualizer {
    /// Takes two String paths: the fuchsia root directory, and the relative path
    /// from the fuchsia root directory to the static assets directory.
    pub fn new(fuchsia_path: String, mut path: String) -> Self {
        if !path.ends_with("/") {
            path = format!("{}/", path);
        }
        let static_root = format!("{}{}", fuchsia_path, path);
        Self { fuchsia_root: fuchsia_path, static_root }
    }

    // If the file specified cannot be found because it does not exist, see
    // if there is an index.html at the specified path and serve that instead.
    pub fn serve_path_or_index(&self, request: Request<Body>) -> Response<Body> {
        fn empty_404() -> Response<Body> {
            let mut response = Response::new(Body::empty());
            *response.status_mut() = StatusCode::NOT_FOUND;
            response
        }

        let mut file = match request.uri().path() {
            "/static/js/third_party/d3.js" => {
                match File::open(Path::new(&self.fuchsia_root).join("scripts/third_party/d3/d3.js"))
                {
                    Ok(file) => file,
                    Err(_) => return empty_404(),
                }
            }
            "/" => match File::open(Path::new(&self.static_root).join("index.html")) {
                Ok(file) => file,
                Err(_) => return empty_404(),
            },
            path => {
                let path = path.strip_prefix("/").unwrap_or(path);
                let base = Path::new(&self.static_root);
                let path = {
                    let mut path = base.join(path);
                    match path.canonicalize() {
                        Ok(path) => path,
                        Err(_) => {
                            if path.set_extension("html") {
                                match path.canonicalize() {
                                    Ok(path) => path,
                                    Err(_) => return empty_404(),
                                }
                            } else {
                                return empty_404();
                            }
                        }
                    }
                };
                if !path.starts_with(base) {
                    return empty_404();
                }
                match File::open(&path) {
                    Ok(file) => file,
                    Err(_) => {
                        return empty_404();
                    }
                }
            }
        };
        let mut buf = Vec::new();
        match file.read_to_end(&mut buf) {
            Ok(_) => (),
            Err(_) => return empty_404(),
        }
        Response::new(buf.into())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, std::fs::File, std::io::Write, tempfile::tempdir};

    fn setup_directory() -> String {
        let dir = tempdir().unwrap();
        let mut foo = File::create(dir.path().join("foo.html")).unwrap();
        foo.write(b"Hello world!").unwrap();
        let mut index = File::create(dir.path().join("index.html")).unwrap();
        index.write(b"This is an index file.").unwrap();

        dir.into_path().into_os_string().into_string().unwrap()
    }

    #[fasync::run_singlethreaded(test)]
    async fn serve_path_or_index_reads_existing_file() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new(String::new(), tmp_dir.clone());

        let request =
            Request::builder().method("GET").uri("/foo.html").body(Body::empty()).unwrap();
        let response = viz.serve_path_or_index(request);
        assert_eq!(response.status(), 200);
        let buffer = hyper::body::to_bytes(response.into_body()).await.unwrap();
        assert_eq!(buffer, b"Hello world!"[..]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn serve_path_or_index_reads_existing_file_with_rewrite() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new(String::new(), tmp_dir.clone());

        let request = Request::builder().method("GET").uri("/foo").body(Body::empty()).unwrap();
        let response = viz.serve_path_or_index(request);
        assert_eq!(response.status(), 200);
        let buffer = hyper::body::to_bytes(response.into_body()).await.unwrap();
        assert_eq!(buffer, b"Hello world!"[..]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn serve_path_or_index_defaults_to_index_file() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new(String::new(), tmp_dir.clone());

        let request = Request::builder().method("GET").body(Body::empty()).unwrap();
        let response = viz.serve_path_or_index(request);
        assert_eq!(response.status(), 200);
        let buffer = hyper::body::to_bytes(response.into_body()).await.unwrap();
        assert_eq!(buffer, b"This is an index file."[..]);
    }

    #[test]
    fn serve_path_or_index_fails_on_missing_file() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new(String::new(), tmp_dir.clone());

        let request =
            Request::builder().method("GET").uri("/bar.html").body(Body::empty()).unwrap();
        let response = viz.serve_path_or_index(request);
        assert_eq!(response.status(), 404);
    }
}
