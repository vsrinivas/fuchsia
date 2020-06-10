// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    regex::Regex,
    std::io::{BufRead, BufReader, Error, ErrorKind, Read, Result, Write},
    std::net::TcpStream,
    std::time::Duration,
};

// Constants/Statics
lazy_static! {
    static ref CONTENT_LENGTH_RE: Regex = Regex::new(r"^Content-Length: (\d+)\r\n$").unwrap();
    static ref STATUS_LINE_RE: Regex = Regex::new(r"^HTTP/1.1 (\d+) (:?[a-zA-Z ]+)\r\n$").unwrap();
    static ref READ_TIMEOUT: Option<Duration> = Some(Duration::new(1, 0));
}
const HTTP_200: usize = 200;

#[async_trait(?Send)]
pub trait PackageGetter {
    async fn read_raw(&self, path: &str) -> Result<Vec<u8>>;
}

/// HTTP Getter object meant to make a simple HTTP GET request to a provided address and path.
/// Meant as a stopover until a more robust HTTP library is brought into the Rust dependency tree.
pub struct HttpGetter {
    address: String,
}

impl HttpGetter {
    pub fn new(address: String) -> Self {
        HttpGetter { address: address }
    }
}

#[async_trait(?Send)]
impl PackageGetter for HttpGetter {
    async fn read_raw(&self, path: &str) -> Result<Vec<u8>> {
        // Connect to package server
        let mut stream = TcpStream::connect(&self.address)?;
        stream.set_read_timeout(*READ_TIMEOUT)?;

        // Make the GET request to the target path
        let get_req_str = format!("GET {} HTTP/1.1\r\nHost: {}\r\n\r\n", path, self.address);
        stream.write(get_req_str.as_bytes())?;
        stream.flush()?;

        let mut reader = BufReader::new(stream);
        let mut content_length: usize = 0;
        // First line must always be the Status-Line
        let mut status_line = String::new();
        reader.read_line(&mut status_line)?;
        if let Some(capture) = STATUS_LINE_RE.captures(&status_line) {
            if let Some(cap) = capture.get(1) {
                let match_str = cap.as_str();
                let match_val = match match_str.parse::<usize>() {
                    Ok(val) => val,
                    Err(_) => {
                        return Err(Error::new(ErrorKind::Other, "Unable to parse response code."));
                    }
                };
                if match_val != HTTP_200 {
                    return Err(Error::new(
                        ErrorKind::InvalidData,
                        format!("Expected 200 response code, received {}.", match_val),
                    ));
                }
            } else {
                return Err(Error::new(ErrorKind::InvalidData, "Could not find a response code."));
            }
        } else {
            return Err(Error::new(ErrorKind::InvalidData, "Could not find status line"));
        }

        // Read headers until we get to an empty line with CRLF which demarcates
        // the end of the HTTP Response Headers.
        loop {
            let mut result = String::new();
            reader.read_line(&mut result)?;

            // Try to read the content length only if we haven't already found it.
            if content_length == 0 {
                // Find the content length to see how much to read in from the body
                // because the stream doesn't EOF after the body.
                if let Some(capture) = CONTENT_LENGTH_RE.captures(&result) {
                    if let Some(cap) = capture.get(1) {
                        let match_str = cap.as_str();
                        content_length = match match_str.parse::<usize>() {
                            Ok(val) => val,
                            Err(_) => {
                                return Err(Error::new(
                                    ErrorKind::Other,
                                    "Unable to parse content length.",
                                ));
                            }
                        };
                    } else {
                        return Err(Error::new(
                            ErrorKind::InvalidData,
                            "Could not find a content length.",
                        ));
                    }
                }
            }

            if result == "\r\n" {
                // Found the end of the headers.
                // Read the response body next which is expected to be exactly
                // `content_length` bytes long.

                if content_length == 0 {
                    // This technically could be valid, if we aren't expecting
                    // anything in the response body.
                    return Ok(Vec::new());
                }
                let mut resp_body = vec![0; content_length];
                reader.read_exact(&mut resp_body)?;
                return Ok(resp_body);
            }
        }
    }
}
