//! Read-only Repository implementation backed by a web server.

use futures_io::AsyncRead;
use futures_util::future::{BoxFuture, FutureExt};
use futures_util::stream::TryStreamExt;
use http::{Response, StatusCode, Uri};
#[cfg(feature = "hyper_013")]
use hyper_013 as hyper;
#[cfg(feature = "hyper_014")]
use hyper_014 as hyper;
use hyper::body::Body;
use hyper::client::connect::Connect;
use hyper::Client;
use hyper::Request;
use percent_encoding::utf8_percent_encode;
use std::io;
use std::marker::PhantomData;
use url::Url;

use crate::crypto::{HashAlgorithm, HashValue};
use crate::error::Error;
use crate::interchange::DataInterchange;
use crate::metadata::{MetadataPath, MetadataVersion, TargetDescription, TargetPath};
use crate::repository::RepositoryProvider;
use crate::util::SafeAsyncRead;
use crate::Result;

/// A builder to create a repository accessible over HTTP.
pub struct HttpRepositoryBuilder<C, D>
where
    C: Connect + Sync + 'static,
    D: DataInterchange,
{
    uri: Uri,
    client: Client<C>,
    user_agent: Option<String>,
    metadata_prefix: Option<Vec<String>>,
    targets_prefix: Option<Vec<String>>,
    min_bytes_per_second: u32,
    _interchange: PhantomData<D>,
}

impl<C, D> HttpRepositoryBuilder<C, D>
where
    C: Connect + Sync + 'static,
    D: DataInterchange,
{
    /// Create a new repository with the given `Url` and `Client`.
    pub fn new(url: Url, client: Client<C>) -> Self {
        HttpRepositoryBuilder {
            uri: url.to_string().parse::<Uri>().unwrap(), // This is dangerous, but will only exist for a short time as we migrate APIs.
            client,
            user_agent: None,
            metadata_prefix: None,
            targets_prefix: None,
            min_bytes_per_second: 4096,
            _interchange: PhantomData,
        }
    }

    /// Create a new repository with the given `Url` and `Client`.
    pub fn new_with_uri(uri: Uri, client: Client<C>) -> Self {
        HttpRepositoryBuilder {
            uri,
            client,
            user_agent: None,
            metadata_prefix: None,
            targets_prefix: None,
            min_bytes_per_second: 4096,
            _interchange: PhantomData,
        }
    }

    /// Set the User-Agent prefix.
    ///
    /// Callers *should* include a custom User-Agent prefix to help maintainers of TUF repositories
    /// keep track of which client versions exist in the field.
    ///
    pub fn user_agent<T: Into<String>>(mut self, user_agent: T) -> Self {
        self.user_agent = Some(user_agent.into());
        self
    }

    /// The argument `metadata_prefix` is used to provide an alternate path where metadata is
    /// stored on the repository. If `None`, this defaults to `/`. For example, if there is a TUF
    /// repository at `https://tuf.example.com/`, but all metadata is stored at `/meta/`, then
    /// passing the arg `Some("meta".into())` would cause `root.json` to be fetched from
    /// `https://tuf.example.com/meta/root.json`.
    pub fn metadata_prefix(mut self, metadata_prefix: Vec<String>) -> Self {
        self.metadata_prefix = Some(metadata_prefix);
        self
    }

    /// The argument `targets_prefix` is used to provide an alternate path where targets is
    /// stored on the repository. If `None`, this defaults to `/`. For example, if there is a TUF
    /// repository at `https://tuf.example.com/`, but all targets are stored at `/targets/`, then
    /// passing the arg `Some("targets".into())` would cause `hello-world` to be fetched from
    /// `https://tuf.example.com/targets/hello-world`.
    pub fn targets_prefix(mut self, targets_prefix: Vec<String>) -> Self {
        self.targets_prefix = Some(targets_prefix);
        self
    }

    /// Set the minimum bytes per second for a read to be considered good.
    pub fn min_bytes_per_second(mut self, min: u32) -> Self {
        self.min_bytes_per_second = min;
        self
    }

    /// Build a `HttpRepository`.
    pub fn build(self) -> HttpRepository<C, D> {
        let user_agent = match self.user_agent {
            Some(user_agent) => user_agent,
            None => "rust-tuf".into(),
        };

        HttpRepository {
            uri: self.uri,
            client: self.client,
            user_agent,
            metadata_prefix: self.metadata_prefix,
            targets_prefix: self.targets_prefix,
            min_bytes_per_second: self.min_bytes_per_second,
            _interchange: PhantomData,
        }
    }
}

/// A repository accessible over HTTP.
pub struct HttpRepository<C, D>
where
    C: Connect + Sync + 'static,
    D: DataInterchange,
{
    uri: Uri,
    client: Client<C>,
    user_agent: String,
    metadata_prefix: Option<Vec<String>>,
    targets_prefix: Option<Vec<String>>,
    min_bytes_per_second: u32,
    _interchange: PhantomData<D>,
}

// Configuration for urlencoding URI path elements.
// From https://url.spec.whatwg.org/#path-percent-encode-set
const URLENCODE_FRAGMENT: &percent_encoding::AsciiSet = &percent_encoding::CONTROLS
    .add(b' ')
    .add(b'"')
    .add(b'<')
    .add(b'>')
    .add(b'`');
const URLENCODE_PATH: &percent_encoding::AsciiSet =
    &URLENCODE_FRAGMENT.add(b'#').add(b'?').add(b'{').add(b'}');

fn extend_uri(uri: Uri, prefix: &Option<Vec<String>>, components: &[String]) -> Result<Uri> {
    let mut uri_parts = uri.into_parts();

    let (path, query) = match &uri_parts.path_and_query {
        Some(path_and_query) => (path_and_query.path(), path_and_query.query()),
        None => ("", None),
    };

    let mut modified_path = path.to_owned();
    if modified_path.ends_with('/') {
        modified_path.pop();
    }

    let mut path_split = modified_path
        .split('/')
        .map(String::from)
        .collect::<Vec<_>>();
    let mut new_path_elements: Vec<&str> = vec![];

    if let Some(ref prefix) = prefix {
        new_path_elements.extend(prefix.iter().map(String::as_str));
    }
    new_path_elements.extend(components.iter().map(String::as_str));

    // Urlencode new items to match behavior of PathSegmentsMut.extend from
    // https://docs.rs/url/2.1.0/url/struct.PathSegmentsMut.html
    let encoded_new_path_elements = new_path_elements
        .into_iter()
        .map(|path_segment| utf8_percent_encode(&path_segment, URLENCODE_PATH).collect());
    path_split.extend(encoded_new_path_elements);
    let constructed_path = path_split.join("/");

    uri_parts.path_and_query =
        match query {
            Some(query) => Some(format!("{}?{}", constructed_path, query).parse().map_err(
                |_| {
                    Error::IllegalArgument(format!(
                        "Invalid path and query: {:?}, {:?}",
                        constructed_path, query
                    ))
                },
            )?),
            None => Some(constructed_path.parse().map_err(|_| {
                Error::IllegalArgument(format!("Invalid URI path: {:?}", constructed_path))
            })?),
        };

    Ok(Uri::from_parts(uri_parts).map_err(|_| {
        Error::IllegalArgument(format!(
            "Invalid URI parts: {:?}, {:?}, {:?}",
            constructed_path, prefix, components
        ))
    })?)
}

impl<C, D> HttpRepository<C, D>
where
    C: Connect + Clone + Send + Sync + 'static,
    D: DataInterchange,
{
    async fn get<'a>(
        &'a self,
        prefix: &'a Option<Vec<String>>,
        components: &'a [String],
    ) -> Result<Response<Body>> {
        let base_uri = self.uri.clone();
        let uri = extend_uri(base_uri, prefix, components)?;

        let req = Request::builder()
            .uri(&uri)
            .header("User-Agent", &*self.user_agent)
            .body(Body::default())?;

        let resp = self.client.request(req).await?;
        let status = resp.status();

        if status == StatusCode::OK {
            Ok(resp)
        } else if status == StatusCode::NOT_FOUND {
            Err(Error::NotFound)
        } else {
            Err(Error::BadHttpStatus {
                code: status,
                uri: uri.to_string(),
            })
        }
    }
}

impl<C, D> RepositoryProvider<D> for HttpRepository<C, D>
where
    C: Connect + Clone + Send + Sync + 'static,
    D: DataInterchange + Send + Sync,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        _max_length: Option<usize>,
        _hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        let components = meta_path.components::<D>(&version);
        async move {
            let resp = self.get(&self.metadata_prefix, &components).await?;

            // TODO(#278) check content length if known and fail early if the payload is too large.

            let reader = resp
                .into_body()
                .map_err(|err| io::Error::new(io::ErrorKind::Other, err))
                .into_async_read()
                .enforce_minimum_bitrate(self.min_bytes_per_second);

            let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(reader);
            Ok(reader)
        }
        .boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        _target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, Result<Box<dyn AsyncRead + Send + Unpin>>> {
        async move {
            let components = target_path.components();
            let resp = self.get(&self.targets_prefix, &components).await?;

            // TODO(#278) check content length if known and fail early if the payload is too large.

            let reader = resp
                .into_body()
                .map_err(|err| io::Error::new(io::ErrorKind::Other, err))
                .into_async_read()
                .enforce_minimum_bitrate(self.min_bytes_per_second);

            Ok(Box::new(reader) as Box<dyn AsyncRead + Send + Unpin>)
        }
        .boxed()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    // Old behavior of the `HttpRepository::get` extension
    // functionality
    fn http_repository_extend_using_url(
        base_url: Url,
        prefix: &Option<Vec<String>>,
        components: &[String],
    ) -> url::Url {
        let mut url = base_url.clone();
        {
            let mut segments = url.path_segments_mut().unwrap();
            if let Some(ref prefix) = prefix {
                segments.extend(prefix);
            }
            segments.extend(components);
        }
        return url;
    }

    #[test]
    fn http_repository_uri_construction() {
        let base_uri = "http://example.com/one";

        let prefix = Some(vec![String::from("prefix")]);
        let components = [
            String::from("components_one"),
            String::from("components_two"),
        ];

        let uri = base_uri.parse::<Uri>().unwrap();
        let extended_uri = extend_uri(uri, &prefix, &components).unwrap();

        let url =
            http_repository_extend_using_url(Url::parse(base_uri).unwrap(), &prefix, &components);

        assert_eq!(url.to_string(), extended_uri.to_string());
        assert_eq!(
            extended_uri.to_string(),
            "http://example.com/one/prefix/components_one/components_two"
        );
    }

    #[test]
    fn http_repository_uri_construction_encoded() {
        let base_uri = "http://example.com/one";

        let prefix = Some(vec![String::from("prefix")]);
        let components = [String::from("chars to encode#?")];
        let uri = base_uri.parse::<Uri>().unwrap();
        let extended_uri = extend_uri(uri, &prefix, &components)
            .expect("correctly generated a URI with a zone id");

        let url =
            http_repository_extend_using_url(Url::parse(base_uri).unwrap(), &prefix, &components);

        assert_eq!(url.to_string(), extended_uri.to_string());
        assert_eq!(
            extended_uri.to_string(),
            "http://example.com/one/prefix/chars%20to%20encode%23%3F"
        );
    }

    #[test]
    fn http_repository_uri_construction_no_components() {
        let base_uri = "http://example.com/one";

        let prefix = Some(vec![String::from("prefix")]);
        let components = [];

        let uri = base_uri.parse::<Uri>().unwrap();
        let extended_uri = extend_uri(uri, &prefix, &components).unwrap();

        let url =
            http_repository_extend_using_url(Url::parse(base_uri).unwrap(), &prefix, &components);

        assert_eq!(url.to_string(), extended_uri.to_string());
        assert_eq!(extended_uri.to_string(), "http://example.com/one/prefix");
    }

    #[test]
    fn http_repository_uri_construction_no_prefix() {
        let base_uri = "http://example.com/one";

        let prefix = None;
        let components = [
            String::from("components_one"),
            String::from("components_two"),
        ];

        let uri = base_uri.parse::<Uri>().unwrap();
        let extended_uri = extend_uri(uri, &prefix, &components).unwrap();

        let url =
            http_repository_extend_using_url(Url::parse(base_uri).unwrap(), &prefix, &components);

        assert_eq!(url.to_string(), extended_uri.to_string());
        assert_eq!(
            extended_uri.to_string(),
            "http://example.com/one/components_one/components_two"
        );
    }

    #[test]
    fn http_repository_uri_construction_with_query() {
        let base_uri = "http://example.com/one?test=1";

        let prefix = None;
        let components = [
            String::from("components_one"),
            String::from("components_two"),
        ];

        let uri = base_uri.parse::<Uri>().unwrap();
        let extended_uri = extend_uri(uri, &prefix, &components).unwrap();

        let url =
            http_repository_extend_using_url(Url::parse(base_uri).unwrap(), &prefix, &components);

        assert_eq!(url.to_string(), extended_uri.to_string());
        assert_eq!(
            extended_uri.to_string(),
            "http://example.com/one/components_one/components_two?test=1"
        );
    }

    #[test]
    fn http_repository_uri_construction_ipv6_zoneid() {
        let base_uri = "http://[aaaa::aaaa:aaaa:aaaa:1234%252]:80";

        let prefix = Some(vec![String::from("prefix")]);
        let components = [
            String::from("componenents_one"),
            String::from("components_two"),
        ];
        let uri = base_uri.parse::<Uri>().unwrap();
        let extended_uri = extend_uri(uri, &prefix, &components)
            .expect("correctly generated a URI with a zone id");
        assert_eq!(
            extended_uri.to_string(),
            "http://[aaaa::aaaa:aaaa:aaaa:1234%252]:80/prefix/componenents_one/components_two"
        );
    }
}
