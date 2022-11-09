// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! link_checker implements the [`DocCheck` trait  used to perform checks on the links and images
//! found in markdown documentation in the Fuchsia project.

use {
    crate::{
        md_element::{CowStr, Element, LinkType},
        DocCheck, DocCheckError, DocCheckerArgs, DocLine,
    },
    anyhow::{bail, Result},
    async_trait::async_trait,
    fuchsia_hyper::{new_https_client_from_tcp_options, HttpsClient, TcpOptions},
    http::{uri::Uri, Request, StatusCode},
    hyper::Body,
    std::{
        collections::{HashMap, HashSet},
        ffi::OsStr,
        path::{self, Path, PathBuf},
    },
    url::Url,
};

// path_help is a wrapper to allow mocking path checks
// exists. and is_dir.
cfg_if::cfg_if! {
    if #[cfg(test)] {
       use crate::mock_path_helper_module as path_helper;
    } else {
       use crate::path_helper_module as path_helper;
    }
}
/// Files that are allowed to link to the documentation host site.
const FILES_ALLOWED_TO_LINK_TO_PUBLISHED_DOCS: [&str; 1] = ["navbar.md"];

const GERRIT_HOST: &str = "fuchsia.googlesource.com";

pub(crate) const PUBLISHED_DOCS_HOST: &str = "fuchsia.dev";

/// List of words that cannot be used as single word ALT text for images.
/// This is a pretty small list (n < 5), but if it grows large, it might be
/// better to manages as an external file vs. inline.
const DISALLOWED_ALT_IMAGE_TEXT: [&str; 1] = [""];
// TODO(fxbug.dev/113039): disallow "drawing, "image" for alt text";

/// List of active repos under fuchsia.googlesource.com which can be linked to.
const VALID_PROJECTS: [&str; 20] = [
    "", // root page of all projects
    "cobalt",
    "drivers", // This is a family of projects.
    "experiences",
    "fargo",
    "fidl-misc",
    "fidlbolt",
    "fontdata",
    "fuchsia",
    "infra", // This is a family of projects, there are sub-repos below this path.
    "integration",
    "intellij-language-fidl",
    "jiri",
    "manifest",
    "third_party", // This is a family of projects, there are sub-repos below this path.
    "topaz",
    "vscode-language-fidl",
    "workstation",
    "samples",
    "sdk-samples", // This is a family of projects, there are sub-repos below this path.
];

/// Top level paths to the published doc site that any page can link to.
/// Links to other locations have to be allowed by adding the source doc
/// page to FILES_ALLOWED_TO_LINK_TO_PUBLISHED_DOCS.
/// "" - is the root of fuchsia.dev
/// "reference" is the generated reference documentation.
/// "schema" is the schema URLs used for parsing.
const PUBLISHED_LINKS_ALLOWED: [&str; 3] = ["", "reference", "schema"];

/// A link (URL, or file path) and it location in the markdown.
#[derive(Debug, Eq, Hash, PartialEq)]
pub struct LinkReference {
    pub link: String,
    pub location: DocLine,
}

/// LinkChecker checks the links and images in markdown files.
/// As the links are checked, links to external websites are collected and optionally
/// checked in the post-check.
#[derive(Debug)]
struct LinkChecker {
    pub root_dir: PathBuf,
    pub project: String,
    pub docs_folder: PathBuf,
    pub check_remote_links: bool,
    links: Vec<LinkReference>,
}

impl LinkChecker {
    /// Takes the raw link from the markdown parser, and normalizes it into
    /// a string that is a filepath or URL.
    fn make_link_to_check(&self, filename: &Path, link_url: &CowStr<'_>) -> Result<String> {
        let link_to_check: String;
        let link = link_url.trim();
        let filename_string = filename.to_string_lossy();

        // relative_filename is relative to the root e.g. /home/googler/fuchsia/docs/file.md
        // is /docs/file.md.  Note that this form (with the leading /) is used a lot because
        // when published, it is at the root of the documentation site.
        //
        // This does cause confusion in this code since PathBuf::join() does not naively join
        // paths with a leading /. See https://doc.rust-lang.org/nightly/std/path/struct.PathBuf.html#method.push
        // for more details.
        let relative_filename =
            filename_string.strip_prefix(self.root_dir.to_string_lossy().as_ref()).unwrap_or("");

        // relative_parent is the directory of the file name, e.g /docs/file.md is /docs.
        let temp_path = PathBuf::from(relative_filename);
        let relative_parent = temp_path.parent().unwrap_or(&self.docs_folder);

        // External links that have any query parameters are decoded when parsed by the markdown parser.
        // To make things easier later, parse the URL and use the encoded parameters.
        if link.starts_with("http://") || link.starts_with("https://") {
            let url: Url = Url::parse(link)?;
            if let Some(query) = url.query() {
                // split on ? from the original string, to avoid complexities
                // to to-stringizing a url without the query params.
                if let Some((first_part, _)) = link.split_once('?') {
                    let encoded_link = format!("{}?{}", first_part, query);
                    link_to_check = encoded_link;
                } else {
                    bail!("Cannot parse {}. Appears to have query parameters, but no ? in the string?", link);
                }
            } else {
                link_to_check = url.to_string();
            }
        } else if link.starts_with('/') {
            // paths are used as-is.
            link_to_check = link.to_string();
        } else if link.starts_with('#') {
            // Anchors are appended to the current file.
            link_to_check = format!("{}{}", self.root_dir.join(relative_filename).display(), link);
        } else {
            // Otherwise, see if it is parsable as a URI, if not, append it to the relative_parent
            // and hope for the best. This usually is something relative like "details-subdir/info.md"
            let uri: Uri = match link.parse() {
                Ok(u) => u,
                Err(_e) => format!("{}/{}", relative_parent.to_string_lossy(), link).parse()?,
            };

            // Check the scheme. If there is one, use it.
            // If there is mailto: (which is commonly used without the //) use the original link
            // Otherwise, it is a relative file path that what parsed.
            link_to_check = match uri.scheme() {
                Some(_) => uri.to_string(),
                None if link.contains("mailto:") => link.to_string(),
                None => format!("{}/{}", relative_parent.to_str().unwrap(), link),
            };
        }
        Ok(link_to_check)
    }
}

#[async_trait]
impl DocCheck for LinkChecker {
    fn name(&self) -> &str {
        "LinkChecker"
    }
    /// Applies the checks for links.
    fn check<'a>(&mut self, element: &'a Element<'_>) -> Result<Option<Vec<DocCheckError>>> {
        let mut errors: Vec<DocCheckError> = vec![];

        // Get all the links from the element. This is needed since the element is commonly
        // a Block or some other collection of elements.
        if let Some(links) = element.get_links() {
            for ele in links {
                let link: &'a CowStr<'a> = match ele {
                    Element::Link(link_type, link_url, _, _, _) => {
                        if link_type == &LinkType::Email || link_url.starts_with("mailto:") {
                            // do nothing.
                            return Ok(None);
                        }
                        link_url
                    }
                    Element::Image(link_type, link_url, _, _, _) => {
                        if link_type == &LinkType::Email || link_url.starts_with("mailto:") {
                            // do nothing.
                            return Ok(None);
                        }
                        // Check for alt text
                        let alt = ele.get_contents().trim().to_string();
                        if DISALLOWED_ALT_IMAGE_TEXT.contains(&alt.as_str()) {
                            errors.push(DocCheckError {
                                doc_line: ele.doc_line(),
                                message: format!(
                                    "Invalid image alt text: {:?}, cannot  be one of {:?}",
                                    alt, DISALLOWED_ALT_IMAGE_TEXT
                                ),
                            })
                        }
                        link_url
                    }
                    _ => {
                        return Ok(None);
                    }
                };

                let link_to_check = self.make_link_to_check(&element.doc_line().file_name, link)?;

                let saw_error = do_check_link(&element.doc_line(), &link_to_check, &self.project)?
                    .map(|err| errors.push(err))
                    .is_some();

                let root_dir = self.root_dir.display().to_string();
                match is_intree_link(&self.project, &root_dir, &self.docs_folder, &link_to_check) {
                    Ok(Some(in_tree_path)) => {
                        if let Some(link_error) = do_in_tree_check(
                            &element.doc_line(),
                            &self.root_dir,
                            &self.docs_folder,
                            &link_to_check,
                            &in_tree_path,
                        ) {
                            errors.push(link_error);
                        }
                    }
                    Ok(None) => {
                        if self.check_remote_links && !saw_error {
                            self.links.push(LinkReference {
                                link: link_to_check.clone(),
                                location: element.doc_line(),
                            });
                        }
                    }
                    Err(e) => errors.push(DocCheckError {
                        doc_line: element.doc_line(),
                        message: format!("{}", e),
                    }),
                };
            }
        }

        if errors.is_empty() {
            Ok(None)
        } else {
            Ok(Some(errors))
        }
    }

    /// At the end, check that the out of tree links work, if requested.
    async fn post_check(&self) -> Result<Option<Vec<DocCheckError>>> {
        let mut errors = vec![];

        if let Some(link_errors) = check_external_links(&self.links).await {
            errors.extend(link_errors);
        }

        if !errors.is_empty() {
            Ok(Some(errors))
        } else {
            Ok(None)
        }
    }
}

/// Checks specific to a link to an in-tree path.
pub(crate) fn do_in_tree_check(
    doc_line: &DocLine,
    root_dir: &Path,
    docs_folder: &Path,
    link_to_check: &str,
    in_tree_path: &Path,
) -> Option<DocCheckError> {
    let filepath = root_dir.join(in_tree_path.strip_prefix("/").unwrap_or(in_tree_path));

    if !path_helper::exists(&filepath) {
        return Some(DocCheckError {
            doc_line: doc_line.clone(),
            message: format!(
                "in-tree link to {} could not be found at {:?}",
                link_to_check, filepath
            ),
        });
    } else if filepath.components().any(|c| c == path::Component::ParentDir) {
        let cannonical_path = match filepath.canonicalize() {
            Ok(p) => p,
            Err(e) => {
                return Some(DocCheckError {
                    doc_line: doc_line.clone(),
                    message: format!("Error canonicalizing path: {:?}: {}", filepath, e),
                })
            }
        };
        if !cannonical_path.starts_with(root_dir) {
            return Some(DocCheckError {
                doc_line: doc_line.clone(),
                message: format!(
                    "relative path {:?} points outside root directory {:?}",
                    in_tree_path, root_dir
                ),
            });
        }
    } else if path_helper::is_dir(&filepath) {
        // If it is a directory to the /docs directory, that directory needs
        // to have a README.md file.
        if in_tree_path
            .components()
            .position(|c| c == path::Component::Normal(OsStr::new(&docs_folder)))
            == Some(1)
        {
            let readme_path = filepath.join("README.md");
            if !path_helper::exists(&readme_path) {
                return Some(DocCheckError {
                    doc_line: doc_line.clone(),
                    message: format!(
                        "in-tree link to {} could not be found at {:?} or  {:?}",
                        link_to_check, filepath, readme_path
                    ),
                });
            }
        }
        // Non-docs paths are OK.
    }
    None
}

/// Parse the link into a Uri, and check that it is either a path or that the http/https
/// links are valid for the host they are pointing to.
pub(crate) fn do_check_link(
    doc_line: &DocLine,
    link: &str,
    project_being_checked: &str,
) -> Result<Option<DocCheckError>> {
    match link.parse::<Uri>() {
        Ok(uri) => {
            match uri.scheme() {
                Some(scheme) => match scheme.as_str() {
                    "http" | "https" => {}
                    _ => return Ok(None),
                },
                None => return Ok(None),
            }
            if let Some(errors) = check_link_authority(doc_line, &uri, project_being_checked) {
                return Ok(Some(errors));
            }

            // Check for host language parameter on google owned urls.
            if uri
                .authority()
                .map(|a| a.host())
                .map(|host| host.ends_with(".dev") || host.ends_with(".google.com"))
                .unwrap_or(false)
                && uri.query().map(|query| query.contains("hl=")).unwrap_or(false)
            {
                return Ok(Some(DocCheckError {
                    doc_line: doc_line.clone(),
                    message: format!("Do not add host language parameter `hl` {}", link),
                }));
            }

            Ok(None)
        }
        Err(e) => Ok(Some(DocCheckError {
            doc_line: doc_line.clone(),
            message: format!("Invalid link {} : {}", link, e),
        })),
    }
}

/// Returns the relative path from the root_dir if the link it to a file in the fuchsia source tree.
pub(crate) fn is_intree_link(
    project: &str,
    root_dir: &str,
    docs_folder: &Path,
    link_to_check: &str,
) -> Result<Option<PathBuf>> {
    if link_to_check.starts_with(root_dir) {
        let mut filepath = link_to_check.strip_prefix(root_dir).unwrap_or(link_to_check);
        (filepath, _) = filepath.split_once('#').unwrap_or((filepath, ""));
        // Split off the query parameters, if any
        (filepath, _) = filepath.split_once('?').unwrap_or((filepath, ""));
        return Ok(Some(PathBuf::from(filepath)));
    } else if link_to_check.starts_with(&format!("https://{}/{}", GERRIT_HOST, project)) {
        let uri: Uri = link_to_check.parse::<Uri>()?;
        let parts = uri.path().split('/').collect::<Vec<&str>>();
        if parts.len() <= 3 {
            let p = parts.join("/");
            if p == format!("/{}/", project) || p == format!("/{}", project) {
                return Ok(None);
            }
        }
        // skip over any branch spec. The first part is empty.
        if parts.len() > 3 && (parts[2] == "+" || parts[2] == "+show") {
            let filepath = PathBuf::from("/");
            if parts[3] == "refs" && parts[4] == "heads" {
                return Ok(Some(filepath.join(parts[6..].join("/"))));
            } else if parts[3] == "HEAD" && parts[4] == docs_folder.to_string_lossy() {
                return Ok(Some(filepath.join(parts[4..].join("/"))));
            } else {
                return Ok(None);
            }
        } else {
            return Ok(Some(PathBuf::from(parts.join("/"))));
        }
    } else if link_to_check.starts_with('/') {
        let (mut filepath, _) = link_to_check.split_once('#').unwrap_or((link_to_check, ""));
        (filepath, _) = filepath.split_once('?').unwrap_or((filepath, ""));

        return match normalize_intree_path(filepath) {
            Ok(normalized) => Ok(Some(normalized)),
            Err(e) => Err(e),
        };
    }
    Ok(None)
}

/// Checks that URLs are not incorrectly pointing to fuchsia.dev or fuchsia.googlesource.com
fn check_link_authority(
    doc_line: &DocLine,
    uri: &Uri,
    project_being_checked: &str,
) -> Option<DocCheckError> {
    let link_to_fuchsia_gerrit_host = match uri.authority() {
        Some(a) => *a == GERRIT_HOST,
        None => false,
    };
    let link_to_published_docs_host = match uri.authority() {
        Some(a) => *a == PUBLISHED_DOCS_HOST,
        None => false,
    };

    let parts = uri.path().split('/').collect::<Vec<&str>>();
    let project = parts[1];

    /*
    Links to gerrit source code should be to HEAD or refs/heads/main or equivalent.
    The links can also not be to unknown or obsolete projects.
     */
    if link_to_fuchsia_gerrit_host {
        if !VALID_PROJECTS.contains(&project) {
            return Some(DocCheckError {
                doc_line: doc_line.clone(),
                message: format!("Obsolete or invalid project {}: {}", project, uri),
            });
        }
        if !on_gerrit_master(uri) && project == project_being_checked {
            let branch_index = parts.iter().position(|x| *x == "+").unwrap();
            let branch_spec = parts[branch_index..branch_index + 2].join("/");

            let recommended = uri.to_string().replace(&branch_spec, "+/HEAD");
            //Possible point of discussion: Non-HEAD links are open discussion for non- //docs links.

            if parts.contains(&"docs") && uri.path().ends_with(".md") {
                return Some(DocCheckError {
                    doc_line: doc_line.clone(),
                    message: format!(
                        "Invalid link to non-master branch: {} consider using {}",
                        uri, recommended
                    ),
                });
            }
        }
    }

    /*
      Links to fuchsia.dev (where the docs are published) should not use http(s):, but
      rather use relative paths from the fuchsia source root. For example, /docs/some/file.md.
      The some projects in fuchsia.dev are allowed to be directly linked since they are not
      part of the markdown checked in.

      There is also a list of exceptions of files that can link via https, these allow gtiles to
      point to fuchsia.dev, and could be removed at some point in the future.
    */
    if link_to_published_docs_host && !PUBLISHED_LINKS_ALLOWED.contains(&project) {
        let base_name = doc_line
            .file_name
            .file_name()
            .unwrap_or_else(|| OsStr::new(""))
            .to_string_lossy()
            .to_string();
        if !FILES_ALLOWED_TO_LINK_TO_PUBLISHED_DOCS.contains(&base_name.as_str()) {
            // If the link is to the published docs directory (fuchsia-src), then
            // a path should be used instead.
            if parts.contains(&"fuchsia-src") {
                return Some(DocCheckError {
                    doc_line: doc_line.clone(),
                    message: format!(
                        "Should not link to {} via {}, use relative filepath",
                        uri,
                        uri.scheme_str().unwrap_or_default()
                    ),
                });
            }
        }
    }
    None
}

/// Checks whether the URI points to the master branch of a Gerrit (i.e.,
/// googlesource.com) project.
fn on_gerrit_master(uri: &Uri) -> bool {
    let path_segments: Vec<&str> =
        uri.path().split('/').skip_while(|p| p != &"+").skip(1).take(3).collect();
    if path_segments.is_empty() {
        // no + branch spec in the URL, so defaults to main.
        return true;
    }
    // Links to gerrit are of the form:
    // https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/docs/README.md
    //  https://fuchsia.googlesource.com/<project>/+/refs/heads/<branch>/<path>

    // path_segments is the path (everything after the server name) split on /
    // branch_index is the index of the + in the path.

    match path_segments[0] {
        "master" | "main" | "HEAD" => true,
        "refs" => {
            // There can be refs/ to other things, so make sure there are at least 3 segments after
            // the +. Then join these together for comparison.
            if 3 < path_segments.len() {
                let ref_path = path_segments[1..4].join("/");
                return ref_path == "refs/heads/main" || ref_path == "refs/heads/master";
            }
            false
        }
        _ => false,
    }
}

fn normalize_intree_path(filepath: &str) -> Result<PathBuf> {
    let orig = PathBuf::from(filepath);
    let mut normalized = PathBuf::new();
    let segments = orig.components();

    for part in segments {
        match part {
            std::path::Component::Prefix(p) => {
                // Prefix is used on Windows systems and is
                // part of the path that is not part of normalizing
                // the path.
                // For Non-windows, it should not appear.
                eprintln!("Unexpected path component {:?}", p);
            }
            std::path::Component::RootDir => {
                //RootDir is the beginning of the Path, after
                // any Prefix.
                normalized.push("/");
            }
            std::path::Component::CurDir => {
                //CurDir is the current directory, "."
                // it is ignored.
            }
            std::path::Component::ParentDir => {
                // ParentDir is the parent of the current item, ".."
                if !normalized.pop() {
                    bail!("Cannot normalize {}, references parent beyond root.", filepath);
                }
            }
            std::path::Component::Normal(p) => normalized.push(p),
        }
    }
    Ok(normalized)
}

pub async fn check_external_links(links: &Vec<LinkReference>) -> Option<Vec<DocCheckError>> {
    // sort the links to take advantage of keep alive
    // HashMap is <authority, set<links>.
    let mut domain_sorted_links = HashMap::<String, HashSet<&LinkReference>>::new();
    let mut errors = vec![];
    for link in links {
        match link.link.parse::<Uri>() {
            Ok(uri) => {
                if let Some(authority) = uri.authority() {
                    let key = authority.to_string();

                    let set = domain_sorted_links.entry(key).or_default();
                    set.insert(link);
                }
            }
            Err(e) => errors.push(DocCheckError {
                doc_line: link.location.clone(),
                message: format!("Error parsing {}: {}", link.link, e),
            }),
        };
    }

    let client: HttpsClient = new_https_client_from_tcp_options(tcp_options());
    for (authority, links) in domain_sorted_links {
        let mut pending_requests = vec![];
        println!("checking {authority} {link_count} links", link_count = links.len());
        for link in links {
            let p = check_url_link(client.clone(), link);
            pending_requests.push(p);
        }
        let results = futures::future::join_all(pending_requests);
        (results.await).into_iter().flatten().for_each(|e| errors.push(e));
    }

    if errors.is_empty() {
        None
    } else {
        Some(errors)
    }
}

/// Check that the URL is valid (200 or 301 or 302).
async fn check_url_link(client: HttpsClient, link: &LinkReference) -> Option<DocCheckError> {
    let request = match Request::get(&link.link).body(Body::from("")) {
        Ok(request) => request,
        Err(e) => {
            return Some(DocCheckError {
                doc_line: link.location.clone(),
                message: format!("Error {} requesting {}", e, link.link),
            })
        }
    };

    match client.request(request).await {
        Ok(response) => match response.status() {
            StatusCode::OK | StatusCode::FOUND | StatusCode::MOVED_PERMANENTLY => None,
            _ => Some(DocCheckError {
                doc_line: link.location.clone(),
                message: format!("Error response {} reading {}", response.status(), &link.link),
            }),
        },
        Err(e) => Some(DocCheckError {
            doc_line: link.location.clone(),
            message: format!("Error {} reading {}", e, link.link),
        }),
    }
}

fn tcp_options() -> TcpOptions {
    let mut options: TcpOptions = std::default::Default::default();

    // Use TCP keepalive to notice stuck connections.
    // After 60s with no data received send a probe every 15s.
    options.keepalive_idle = Some(std::time::Duration::from_secs(60));
    options.keepalive_interval = Some(std::time::Duration::from_secs(15));
    // After 8 probes go unacknowledged treat the connection as dead.
    options.keepalive_count = Some(8);

    options
}

/// Called from main to register all the checks to preform which are implemented in this module.
pub(crate) fn register_markdown_checks(opt: &DocCheckerArgs) -> Result<Vec<Box<dyn DocCheck>>> {
    let checker = LinkChecker {
        root_dir: opt.root.clone(),
        project: opt.project.clone(),
        docs_folder: opt.docs_folder.clone(),
        check_remote_links: !opt.local_links_only,
        links: vec![],
    };
    Ok(vec![Box::new(checker)])
}

#[cfg(test)]
mod tests {
    use {super::*, crate::DocContext};

    #[test]
    fn test_make_link_to_check() -> Result<()> {
        let checker = LinkChecker {
            root_dir: PathBuf::from("/my/root/fuchsia"),
            project: "fuchsia".to_string(),
            docs_folder: PathBuf::from("docs"),
            check_remote_links: false,
            links: vec![],
        };
        let filename = PathBuf::from("/my/root/fuchsia/docs/index.md");

        let test_data = [
            ("README.md", "/docs/README.md"),
            ("https://my-server.com", "https://my-server.com/"),
            (
                "http://my-server.com/page?qp=1&words=one two three",
                "http://my-server.com/page?qp=1&words=one%20two%20three",
            ),
            ("/docs/some-file.md", "/docs/some-file.md"),
            ("#Anchor-name", "/docs/index.md#Anchor-name"),
            ("path/to/sub/info.md", "/docs/path/to/sub/info.md"),
            ("mailto:someone@somewhere.tld", "mailto:someone@somewhere.tld"),
            ("https:///bad-url?x=", "https:///bad-url?x="),
        ];

        for (input, expected) in test_data {
            let actual =
                checker.make_link_to_check(filename.as_path(), &CowStr::Borrowed(input))?;

            assert_eq!(actual, expected)
        }
        Ok(())
    }

    #[test]
    fn test_normalize_intree_path() -> Result<()> {
        let test_data = [
            ("/docs", PathBuf::from("/docs")),
            ("/docs/../docs", PathBuf::from("/docs")),
            ("/docs/sub/location.md", PathBuf::from("/docs/sub/location.md")),
            ("/docs/sub/two/../location.md", PathBuf::from("/docs/sub/location.md")),
            ("/docs/sub/./location.md", PathBuf::from("/docs/sub/location.md")),
        ];

        for (data, expected) in test_data {
            let actual = normalize_intree_path(data)?;
            assert_eq!(actual, expected);
        }

        Ok(())
    }

    #[test]
    fn test_happy_path() -> Result<()> {
        let opt = DocCheckerArgs {
            root: PathBuf::from("/path/to/fuchsia"),
            project: "fuchsia".to_string(),
            docs_folder: PathBuf::from("docs"),
            local_links_only: true,
        };

        let mut checks = register_markdown_checks(&opt)?;
        assert_eq!(checks.len(), 1);

        let ctx = DocContext::new(
            PathBuf::from("/docs/README.md"),
            "This is a line to [something](/docs/something.md",
        );

        if let Some(check) = checks.first_mut() {
            for ele in ctx {
                let errors = check.check(&ele)?;
                assert!(errors.is_none(), "expected none, got {:?}", errors);
            }
        }

        Ok(())
    }

    #[test]
    fn test_is_in_tree_link() -> Result<()> {
        let root_dir = "/some/root/dir";
        let project = "fuchsia";
        let docs_folder = PathBuf::from("docs");

        let test_cases = [
    ("/docs/README.md", Some(PathBuf::from("/docs/README.md"))),
    ("/docs/somewhere/file.md#header1", Some(PathBuf::from("/docs/somewhere/file.md"))),
    ("https://google.com", None),
    ("mailto:someone@email.com", None),
    ("/src/to/a/program.cc", Some(PathBuf::from("/src/to/a/program.cc"))),
    ("https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/lib/fdio", None),
    ("https://fuchsia.googlesource.com/fuchsia/docs/README.md", Some(PathBuf::from("/fuchsia/docs/README.md"))),
    ("https://fuchsia.googlesource.com/fuchsia/+/7461d8882167e7a9d1b494e3b1734d2c063830fc/build/package.gni#604", None),
    ("https://fuchsia.googlesource.com/fuchsia/+show/HEAD/docs/concepts/kernel/_toc.yaml", Some(PathBuf::from("/docs/concepts/kernel/_toc.yaml"))),
    ("https://fuchsia.googlesource.com/fuchsia", None),
    ("https://fuchsia.googlesource.com/fuchsia/", None)


  ];
        for (link_to_check, expected) in test_cases {
            let result = is_intree_link(project, root_dir, &docs_folder, link_to_check)?;
            assert_eq!(result, expected);
        }
        Ok(())
    }

    #[test]
    fn test_check() -> Result<()> {
        let opt = DocCheckerArgs {
            root: PathBuf::from("/path/to/fuchsia"),
            project: "fuchsia".to_string(),
            docs_folder: PathBuf::from("docs"),
            local_links_only: true,
        };

        let mut checks = register_markdown_checks(&opt)?;
        assert_eq!(checks.len(), 1);

        let test_data: Vec<(DocContext<'_>, Option<Vec<DocCheckError>>)> = vec![
            (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "This is a line to [something](/docs/something.md",
            ),
            None,
        ),
        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "invalid image text ![](/docs/something.png)",
            ),
            Some(
                [DocCheckError::new(1, PathBuf::from("/docs/README.md"),
                    "Invalid image alt text: \"\", cannot  be one of [\"\"]"),
                 DocCheckError::new(1,PathBuf::from("/docs/README.md"),
                   "in-tree link to /docs/something.png could not be found at \"/path/to/fuchsia/docs/something.png\"")].to_vec()),
        ),
        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "invalid url [oops](https:///nowhere/something.md?xx)",
            ),
            Some([DocCheckError::new(1, PathBuf::from("/docs/README.md"),
             "Invalid link https:///nowhere/something.md?xx : invalid format")].to_vec())
        ),
        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "relative path outside root  [oops](/docs/../../illegal.md)",
            ),
            Some([DocCheckError::new(1,PathBuf::from("/docs/README.md"),
             "Cannot normalize /docs/../../illegal.md, references parent beyond root.")].to_vec())
        ),
        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "hl param is not allowed [hl](https://google.com/something?hl=en)",
            ),
            None,
        ),
        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "invalid project link [garnet](https://fuchsia.googlesource.com/garnet/+/HEAD/src/file.cc)",
            ),
            Some([DocCheckError::new(1, PathBuf::from("/docs/README.md"),
             "Obsolete or invalid project garnet: https://fuchsia.googlesource.com/garnet/+/HEAD/src/file.cc")].to_vec())
        ),

        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "non-master branch link to docs [old doc](https://fuchsia.googlesource.com/fuchsia/+/some-branch/docs/file.md)",
            ),
            Some([DocCheckError::new(1,PathBuf::from("/docs/README.md"),
              "Invalid link to non-master branch: https://fuchsia.googlesource.com/fuchsia/+/some-branch/docs/file.md consider using https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/file.md")].to_vec())
        ),
        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "non-master branch link to src ok [old source](https://fuchsia.googlesource.com/fuchsia/+/some-branch/tools/file.cc)",
            ),
            None,
        ),
        (
            DocContext::new(
                PathBuf::from("/docs/README.md"),
                "non-markdown file OK to link to docs [non-source](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/docs/OWNERS)",
            ),
            None,
        )
        ];

        for (ctx, expected_errors) in test_data {
            for ele in ctx {
                let errors = checks[0].check(&ele)?;
                if let Some(ref expected_list) = expected_errors {
                    let mut expected_iter = expected_list.iter();
                    if let Some(actual_errors) = errors {
                        for actual in actual_errors {
                            if let Some(expected) = expected_iter.next() {
                                assert_eq!(&actual, expected);
                            } else {
                                panic!("Got unexpected error returned: {:?}", actual);
                            }
                        }
                        let unused_errors: Vec<&DocCheckError> = expected_iter.collect();
                        if !unused_errors.is_empty() {
                            panic!("Expected more errors: {:?}", unused_errors);
                        }
                    } else if expected_errors.is_some() {
                        panic!("No errors, but expected {:?}", expected_errors);
                    }
                } else if errors.is_some() {
                    panic!("Got unexpected errors {:?}", errors.unwrap());
                }
            }
        }

        Ok(())
    }

    #[test]
    fn test_do_intree_check() -> Result<()> {
        let doc_line = DocLine { line_num: 1, file_name: PathBuf::from("some/file.md") };
        let root_dir = PathBuf::from("/path/to/fuchsia");
        let docs_folder = PathBuf::from("docs");

        let test_data = [
                   ("/docs/exists/something.md", "/docs/exists/something.md", None),
               ("/docs/no_readme", "/docs/no_readme", Some(DocCheckError::new(
                 1, PathBuf::from("some/file.md"),
                  "in-tree link to /docs/no_readme could not be found at \"/path/to/fuchsia/docs/no_readme\" or  \"/path/to/fuchsia/docs/no_readme/README.md\"")))];

        for (link_to_check, in_tree_path, expected_error) in test_data {
            let result = do_in_tree_check(
                &doc_line,
                &root_dir,
                &docs_folder,
                link_to_check,
                &PathBuf::from(in_tree_path),
            );
            assert_eq!(result, expected_error);
        }
        Ok(())
    }
}
