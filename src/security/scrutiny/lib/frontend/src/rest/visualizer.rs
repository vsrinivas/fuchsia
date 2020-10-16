// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rouille::{Request, Response};

/// Returns a tuple consisting of (PATH_PREFIX_TO_REMOVE_FROM_REQUEST, PATH_RELATIVE_TO_FUCHSIA_ROOT)
/// of the location of the file specified by the url.
/// This is used in the case where asset files are not contained in the
/// configured static directory.
fn translate_hardcoded_path(url: &str) -> Option<(String, String)> {
    match url {
        "/static/js/third_party/d3.js" => {
            Some(("/static/js/third_party/".to_string(), "/scripts/third_party/d3".to_string()))
        }
        _ => None,
    }
}

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
        Self { fuchsia_root: fuchsia_path, static_root: static_root }
    }

    // If the file specified cannot be found because it does not exist, see
    // if there is an index.html at the specified path and serve that instead.
    pub fn serve_path_or_index(&self, request: &Request) -> Response {
        if let Some((prefix_to_remove, path)) = translate_hardcoded_path(&request.url()) {
            // Hardcoded paths are relative to fuchsia_root.
            let prefix_stripped_req = request
                .remove_prefix(&prefix_to_remove)
                .expect("Expected to be able to remove prefix, but was unable to.");
            rouille::match_assets(&prefix_stripped_req, &format!("{}{}", self.fuchsia_root, path))
        } else {
            let response = rouille::match_assets(request, &self.static_root);
            // Looking for a *.html file if we were unable to find a matching asset.
            if response.status_code == 404 {
                let mut modified_url = request.url();
                if modified_url.is_empty() || modified_url == "/" {
                    modified_url = "/index.html".to_string();
                } else {
                    modified_url = format!("{}.html", request.url());
                }
                let new_request = Request::fake_http_from(
                    *request.remote_addr(),
                    request.method(),
                    modified_url,
                    request
                        .headers()
                        .map(|(key, val)| (key.to_string(), val.to_string()))
                        .collect(),
                    vec![],
                ); // Since this should be a GET request for an index.html, we never care about the body.
                rouille::match_assets(&new_request, &self.static_root)
            } else {
                response
            }
        }
    }
}

#[cfg(test)]
mod tests {

    use {super::*, std::fs::File, std::io::Write, tempfile::tempdir};

    fn setup_directory() -> String {
        let dir = tempdir().unwrap();
        let mut foo = File::create(dir.path().join("foo.html")).unwrap();
        foo.write(b"Hello world!").unwrap();
        let mut index = File::create(dir.path().join("index.html")).unwrap();
        index.write(b"This is an index file.").unwrap();

        dir.into_path().into_os_string().into_string().unwrap()
    }

    #[test]
    fn serve_path_or_index_reads_existing_file() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new("".to_string(), tmp_dir.clone());

        let response =
            viz.serve_path_or_index(&Request::fake_http("GET", "foo.html", vec![], vec![]));
        assert_eq!(response.status_code, 200);
        let mut buffer = Vec::new();
        let (mut reader, _) = response.data.into_reader_and_size();
        reader.read_to_end(&mut buffer).unwrap();
        assert_eq!(buffer, b"Hello world!");
    }

    #[test]
    fn serve_path_or_index_reads_existing_file_with_rewrite() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new("".to_string(), tmp_dir.clone());

        let response = viz.serve_path_or_index(&Request::fake_http("GET", "foo", vec![], vec![]));
        assert_eq!(response.status_code, 200);
        let mut buffer = Vec::new();
        let (mut reader, _) = response.data.into_reader_and_size();
        reader.read_to_end(&mut buffer).unwrap();
        assert_eq!(buffer, b"Hello world!");
    }

    #[test]
    fn serve_path_or_index_defaults_to_index_file() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new("".to_string(), tmp_dir.clone());

        let response = viz.serve_path_or_index(&Request::fake_http("GET", "", vec![], vec![]));
        assert_eq!(response.status_code, 200);
        let mut buffer = Vec::new();
        let (mut reader, _) = response.data.into_reader_and_size();
        reader.read_to_end(&mut buffer).unwrap();
        assert_eq!(buffer, b"This is an index file.");
    }

    #[test]
    fn serve_path_or_index_fails_on_missing_file() {
        let tmp_dir = setup_directory();
        let viz = Visualizer::new("".to_string(), tmp_dir.clone());

        let response =
            viz.serve_path_or_index(&Request::fake_http("GET", "bar.html", vec![], vec![]));
        assert_eq!(response.status_code, 404);
    }
}
