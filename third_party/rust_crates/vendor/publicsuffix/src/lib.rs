//! Robust domain name parsing using the Public Suffix List
//!
//! This library allows you to easily and accurately parse any given domain name.
//!
//! ## Examples
//!
//! ```rust,norun
//! extern crate publicsuffix;
//!
//! use publicsuffix::List;
//! # use publicsuffix::Result;
//!
//! # fn examples() -> Result<()> {
//! // Fetch the list from the official URL,
//! # #[cfg(feature = "remote_list")]
//! let list = List::fetch()?;
//!
//! // from your own URL
//! # #[cfg(feature = "remote_list")]
//! let list = List::from_url("https://example.com/path/to/public_suffix_list.dat")?;
//!
//! // or from a local file.
//! let list = List::from_path("/path/to/public_suffix_list.dat")?;
//!
//! // Using the list you can find out the root domain
//! // or extension of any given domain name
//! let domain = list.parse_domain("www.example.com")?;
//! assert_eq!(domain.root(), Some("example.com"));
//! assert_eq!(domain.suffix(), Some("com"));
//!
//! let domain = list.parse_domain("www.食狮.中国")?;
//! assert_eq!(domain.root(), Some("食狮.中国"));
//! assert_eq!(domain.suffix(), Some("中国"));
//!
//! let domain = list.parse_domain("www.xn--85x722f.xn--55qx5d.cn")?;
//! assert_eq!(domain.root(), Some("xn--85x722f.xn--55qx5d.cn"));
//! assert_eq!(domain.suffix(), Some("xn--55qx5d.cn"));
//!
//! let domain = list.parse_domain("a.b.example.uk.com")?;
//! assert_eq!(domain.root(), Some("example.uk.com"));
//! assert_eq!(domain.suffix(), Some("uk.com"));
//!
//! let name = list.parse_dns_name("_tcp.example.com.")?;
//! assert_eq!(name.domain().and_then(|domain| domain.root()), Some("example.com"));
//! assert_eq!(name.domain().and_then(|domain| domain.suffix()), Some("com"));
//!
//! // You can also find out if this is an ICANN domain
//! assert!(!domain.is_icann());
//!
//! // or a private one
//! assert!(domain.is_private());
//!
//! // In any case if the domain's suffix is in the list
//! // then this is definately a registrable domain name
//! assert!(domain.has_known_suffix());
//! # Ok(())
//! # }
//! # fn main() {}
//! ```

#![recursion_limit = "1024"]

#[macro_use]
extern crate error_chain;
#[cfg(feature = "remote_list")]
extern crate native_tls;
#[macro_use]
extern crate lazy_static;
extern crate regex;
extern crate idna;
extern crate url;

pub mod errors;

#[cfg(feature = "remote_list")]
#[cfg(test)]
mod tests;

use std::fs::File;
use std::path::Path;
#[cfg(feature = "remote_list")]
use std::time::Duration;
#[cfg(feature = "remote_list")]
use std::net::TcpStream;
use std::io::Read;
#[cfg(feature = "remote_list")]
use std::io::Write;
use std::collections::HashMap;
use std::net::IpAddr;
use std::str::FromStr;
use std::fmt;

pub use errors::{Result, Error};

use regex::RegexSet;
use errors::{ErrorKind, ResultExt};
#[cfg(feature = "remote_list")]
use native_tls::TlsConnector;
use idna::{domain_to_unicode, uts46};
use url::Url;

/// The official URL of the list
pub const LIST_URL: &'static str = "https://publicsuffix.org/list/public_suffix_list.dat";

#[derive(Debug, PartialEq, Eq, Hash)]
struct Suffix {
    rule: String,
    typ: Type,
}

/// Stores the public suffix list
///
/// You can use the methods, `fetch`, `from_url` or `from_path` to build the list.
/// If you are using this in a long running server it's recommended you use either
/// `fetch` or `from_url` to download updates at least once a week.
#[derive(Debug)]
pub struct List {
    rules: HashMap<String, Vec<Suffix>>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum Type {
    Icann,
    Private,
}

/// Holds information about a particular domain
///
/// This is created by `List::parse_domain`.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct Domain {
    full: String,
    typ: Option<Type>,
    suffix: Option<String>,
    registrable: Option<String>,
}

/// Holds information about a particular host
///
/// This is created by `List::parse_host`.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum Host {
    Ip(IpAddr),
    Domain(Domain),
}

/// Holds information about a particular DNS name
///
/// This is created by `List::parse_dns_name`.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct DnsName {
    name: String,
    domain: Option<Domain>,
}

lazy_static! {
    // Regex for matching domain name labels
    static ref LABEL: RegexSet = {
        let exprs = vec![
            // can be any combination of alphanumeric characters
            r"^[[:alnum:]]+$",
            // or it can start with an alphanumeric character
            // then optionally be followed by any combination of
            // alphanumeric characters and dashes before finally
            // ending with an alphanumeric character
            r"^[[:alnum:]]+[[:alnum:]-]*[[:alnum:]]+$",
        ];
        RegexSet::new(exprs).unwrap()
    };

    // Regex for matching the local-part of an
    // email address
    static ref LOCAL: RegexSet = {
        // these characters can be anywhere in the expresion
        let global = r#"[[:alnum:]!#$%&'*+/=?^_`{|}~-]"#;
        // non-ascii characters (an also be unquoted)
        let non_ascii = r#"[^\x00-\x7F]"#;
        // the pattern to match
        let quoted = r#"["(),\\:;<>@\[\]. ]"#;
        // combined regex
        let combined = format!(r#"({}*{}*)"#, global, non_ascii);

        let exprs = vec![
            // can be any combination of allowed characters
            format!(r#"^{}+$"#, combined),
            // can be any combination of allowed charaters
            // separated by a . in between
            format!(r#"^({0}+[.]?{0}+)+$"#, combined),
            // can be a quoted string with allowed plus
            // additional characters
            format!(r#"^"({}*{}*)*"$"#, combined, quoted),
        ];

        RegexSet::new(exprs).unwrap()
    };
}

/// Converts a type into a Url object
pub trait IntoUrl {
    fn into_url(self) -> Result<Url>;
}

impl IntoUrl for Url {
    fn into_url(self) -> Result<Url> {
        Ok(self)
    }
}

impl<'a> IntoUrl for &'a str {
    fn into_url(self) -> Result<Url> {
        Ok(Url::parse(self)?)
    }
}

impl<'a> IntoUrl for &'a String {
    fn into_url(self) -> Result<Url> {
        Ok(Url::parse(self)?)
    }
}

impl IntoUrl for String {
    fn into_url(self) -> Result<Url> {
        Ok(Url::parse(&self)?)
    }
}

#[cfg(feature = "remote_list")]
fn request<U: IntoUrl>(u: U) -> Result<String> {
    let url = u.into_url()?;
    let addr = url.with_default_port(|_| Err(()))?;
    let host = match url.host_str() {
        Some(host) => host,
        None => { return Err(ErrorKind::NoHost.into()); }
    };
    let data = format!("GET {} HTTP/1.0\r\nHost: {}\r\n\r\n", url.path(), host);
    let stream = TcpStream::connect(addr)?;
    let timeout = Duration::from_secs(2);
    stream.set_read_timeout(Some(timeout))?;
    stream.set_write_timeout(Some(timeout))?;

    let mut res = String::new();

    match url.scheme() {
        scheme if scheme == "https" => {
            let connector = TlsConnector::builder().build()?;
            let mut stream = connector.connect(host, stream)?;
            stream.write_all(data.as_bytes())?;
            stream.read_to_string(&mut res)?;
        }
        scheme if scheme == "http" => {
            let mut stream = stream;
            stream.write_all(data.as_bytes())?;
            stream.read_to_string(&mut res)?;
        }
        _ => { return Err(ErrorKind::UnsupportedScheme.into()); }
    }

    Ok(res)
}

impl List {
    fn append(&mut self, rule: &str, typ: Type) -> Result<()> {
        rule.rsplit('.').next()
            .ok_or(ErrorKind::InvalidRule(rule.into()).into())
            .and_then(|tld| {
                if tld.is_empty() {
                    return Err(ErrorKind::InvalidRule(rule.into()).into());
                }
                Ok(tld)})
            .and_then(|tld| {
                self.rules.entry(tld.into()).or_insert(Vec::new())
                    .push(Suffix {
                        rule: rule.into(),
                        typ: typ,
                    });
                Ok(())
            })
    }

    fn build(res: &str) -> Result<List> {
        let mut typ = None;
        let mut list = List::empty();
        for line in res.lines() {
            match line {
                line if line.contains("BEGIN ICANN DOMAINS") => { typ = Some(Type::Icann); }
                line if line.contains("BEGIN PRIVATE DOMAINS") => { typ = Some(Type::Private); }
                line if line.starts_with("//") => { continue; }
                line => {
                    match typ {
                        Some(typ) => {
                            let rule = match line.split_whitespace().next() {
                                Some(rule) => rule,
                                None => continue,
                            };
                            list.append(rule, typ)?;
                        }
                        None => { continue; }
                    }
                }
            }
        }
        if list.rules.is_empty() || list.all().is_empty() {
            return Err(ErrorKind::InvalidList.into());
        }
        Ok(list)
    }

    /// Creates an empty List without any rules
    ///
    /// Sometimes all you want is to do syntax checks. If you don't really care whether
    /// the domain has a known suffix or not you can just create an empty list and use
    /// that to parse domain names and email addresses.
    pub fn empty() -> List {
        List {
            rules: HashMap::new(),
        }
    }

    /// Pull the list from a URL
    #[cfg(feature = "remote_list")]
    pub fn from_url<U: IntoUrl>(url: U) -> Result<List> {
        request(url).and_then(Self::from_string)
    }

    /// Fetch the list from a local file
    pub fn from_path<P: AsRef<Path>>(path: P) -> Result<List> {
        File::open(path)
            .map_err(|err| ErrorKind::Io(err).into())
            .and_then(|mut data| {
                let mut res = String::new();
                data.read_to_string(&mut res)?;
                Ok(res)
            })
            .and_then(Self::from_string)
    }

    /// Build the list from the result of anything that implements `std::io::Read`
    ///
    /// If you don't already have your list on the filesystem but want to use your
    /// own library to fetch the list you can use this method so you don't have to
    /// save it first.
    pub fn from_reader<R: Read>(mut reader: R) -> Result<List> {
        let mut res = String::new();
        reader.read_to_string(&mut res)?;
        Self::build(&res)
    }

    /// Build the list from a string
    ///
    /// The list doesn't always have to come from a file. You can maintain your own
    /// list, say in a DBMS. You can then pull it at runtime and build the list from
    /// the resulting String.
    pub fn from_string(string: String) -> Result<List> {
        Self::from_str(&string)
    }

    /// Build the list from a str
    ///
    /// The list doesn't always have to come from a file. You can maintain your own
    /// list, say in a DBMS. You can then pull it at runtime and build the list from
    /// the resulting str.
    pub fn from_str(string: &str) -> Result<List> {
        Self::build(string)
    }

    /// Pull the list from the official URL
    #[cfg(feature = "remote_list")]
    pub fn fetch() -> Result<List> {
        let github = "https://raw.githubusercontent.com/publicsuffix/list/master/public_suffix_list.dat";

        Self::from_url(LIST_URL)
            // Fallback to the Github repo if the official link
            // is down for some reason.
            .or_else(|_| Self::from_url(github))
    }

    fn find_type(&self, typ: Type) -> Vec<&str> {
        self.rules.values()
            .fold(Vec::new(), |mut res, ref suffices| {
                for suffix in *suffices {
                    if suffix.typ == typ {
                        res.push(&suffix.rule);
                    }
                }
                res
            })
    }

    /// Gets a list of all ICANN domain suffices
    pub fn icann(&self) -> Vec<&str> {
        self.find_type(Type::Icann)
    }

    /// Gets a list of all private domain suffices
    pub fn private(&self) -> Vec<&str> {
        self.find_type(Type::Private)
    }

    /// Gets a list of all domain suffices
    pub fn all(&self) -> Vec<&str> {
        self.rules.values()
            .fold(Vec::new(), |mut res, ref suffices| {
                for suffix in *suffices {
                    res.push(&suffix.rule);
                }
                res
            })
    }

    /// Parses a domain using the list
    pub fn parse_domain(&self, domain: &str) -> Result<Domain> {
        Domain::parse(domain, self, true)
    }

    /// Parses a host using the list
    ///
    /// A host, for the purposes of this library, is either
    /// an IP address or a domain name.
    pub fn parse_host(&self, host: &str) -> Result<Host> {
        Host::parse(host, self)
    }

    /// Extracts Host from a URL
    pub fn parse_url<U: IntoUrl>(&self, url: U) -> Result<Host> {
        let url = url.into_url()?;
        match url.scheme() {
            "mailto" => {
                match url.host_str() {
                    Some(host) => self.parse_email(&format!("{}@{}", url.username(), host)),
                    None => Err(ErrorKind::InvalidEmail.into()),
                }
            }
            _ => {
                match url.host_str() {
                    Some(host) => self.parse_host(host),
                    None => Err(ErrorKind::NoHost.into()),
                }
            }
        }
    }

    /// Extracts Host from an email address
    ///
    /// This method can also be used, simply to validate an email address.
    /// If it returns an error, the email address is not valid.
    // https://en.wikipedia.org/wiki/Email_address#Syntax
    // https://en.wikipedia.org/wiki/International_email#Email_addresses
    // http://girders.org/blog/2013/01/31/dont-rfc-validate-email-addresses/
    // https://html.spec.whatwg.org/multipage/forms.html#valid-e-mail-address
    // https://hackernoon.com/the-100-correct-way-to-validate-email-addresses-7c4818f24643#.pgcir4z3e
    // http://haacked.com/archive/2007/08/21/i-knew-how-to-validate-an-email-address-until-i.aspx/
    // https://tools.ietf.org/html/rfc6530#section-10.1
    // http://rumkin.com/software/email/rules.php
    pub fn parse_email(&self, address: &str) -> Result<Host> {
        let mut parts = address.rsplitn(2, "@");
        let host = match parts.next() {
            Some(host) => host,
            None => { return Err(ErrorKind::InvalidEmail.into()); }
        };
        let local = match parts.next() {
            Some(local) => local,
            None => { return Err(ErrorKind::InvalidEmail.into()); }
        };
        if local.chars().count() > 64
            || address.chars().count() > 254
            || (!local.starts_with('"') && local.contains(".."))
            || !LOCAL.is_match(local)
        {
            return Err(ErrorKind::InvalidEmail.into());
        }
        self.parse_host(host)
    }

    /// Parses any arbitrary string
    ///
    /// Effectively this means that the string is either a URL, an email address or a host.
    pub fn parse_str(&self, string: &str) -> Result<Host> {
        if string.contains("://") {
            self.parse_url(string)
        } else if string.contains("@") {
            self.parse_email(string)
        } else {
            self.parse_host(string)
        }
    }

    /// Parses any arbitrary string that can be used as a key in a DNS database
    pub fn parse_dns_name(&self, name: &str) -> Result<DnsName> {
        let mut dns_name = DnsName {
            name: Domain::to_ascii(name).chain_err(|| {
                ErrorKind::InvalidDomain(name.into())
            })?,
            domain: None,
        };
        if let Ok(mut domain) = Domain::parse(name, self, false) {
            if let Some(root) = domain.root().map(|root| root.to_string()) {
                if Domain::has_valid_syntax(&root) {
                    domain.full = root;
                    dns_name.domain = Some(domain);
                }
            }
        }
        Ok(dns_name)
    }
}

impl Host {
    fn parse(mut host: &str, list: &List) -> Result<Host> {
        if let Ok(domain) = Domain::parse(host, list, true) {
            return Ok(Host::Domain(domain));
        }
        if host.starts_with("[") 
            && !host.starts_with("[[")
                && host.ends_with("]")
                && !host.ends_with("]]")
                {
                    host = host
                        .trim_left_matches("[")
                        .trim_right_matches("]");
                };
        if let Ok(ip) = IpAddr::from_str(host) {
            return Ok(Host::Ip(ip));
        }
        Err(ErrorKind::InvalidHost.into())
    }

    /// A convenient method to simply check if a host is an IP address
    pub fn is_ip(&self) -> bool {
        if let &Host::Ip(_) = self {
            return true;
        }
        false
    }

    /// A convenient method to simply check if a host is a domain name
    pub fn is_domain(&self) -> bool {
        if let &Host::Domain(_) = self {
            return true;
        }
        false
    }
}

impl fmt::Display for Host {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            &Host::Ip(ref ip) => write!(f, "{}", ip),
            &Host::Domain(ref domain) => write!(f, "{}", domain),
        }
    }
}

impl Domain {
    /// Check if a domain has valid syntax
    // https://en.wikipedia.org/wiki/Domain_name#Domain_name_syntax
    // http://blog.sacaluta.com/2011/12/dns-domain-names-253-or-255-bytesoctets.html
    // https://blogs.msdn.microsoft.com/oldnewthing/20120412-00/?p=7873/
    pub fn has_valid_syntax(domain: &str) -> bool {
        // we are explicitly checking for this here before calling `domain_to_ascii`
        // because `domain_to_ascii` strips of leading dots so we won't be able to
        // check for this later
        if domain.starts_with('.') { return false; }
        // let's convert the domain to ascii early on so we can validate
        // internationalised domain names as well
        let domain = match Self::to_ascii(domain) {
            Ok(domain) => { domain }
            Err(_) => { return false; }
        };
        let mut labels: Vec<&str> = domain.split('.').collect();
        // strip of the first dot from a domain to support fully qualified domain names
        if domain.ends_with(".") { labels.pop(); }
        // a domain must not have more than 127 labels
        if labels.len() > 127 { return false; }
        labels.reverse();
        for (i, label) in labels.iter().enumerate() {
            // the tld must not be a number
            if i == 0 && label.parse::<f64>().is_ok() { return false; }
            // any label must only contain allowed characters
            if !LABEL.is_match(label) { return false; }
        }
        true
    }

    fn find_possible_matches<'a>(domain: &str, list: &'a List) -> Result<Vec<&'a Suffix>> {
        let tld = match domain.rsplit('.').next() {
            Some(tld) => {
                if tld.is_empty() { return Ok(Vec::new()); }
                tld
            },
            None => { return Ok(Vec::new()); },
        };
        let candidates = match list.rules.get(tld) {
            Some(candidates) => candidates,
            None => { return Ok(Vec::new()); },
        };
        let candidates = candidates.iter()
            .fold(Vec::new(), |mut res, ref suffix| {
                res.push(*suffix);
                res
            });
        Ok(candidates)
    }

    fn assemble(input: &str, s_len: usize) -> String {
        let domain = input.to_lowercase();

        let d_labels: Vec<&str> = domain
            .trim_right_matches('.')
            .split('.').rev().collect();

        (&d_labels[..s_len]).iter().rev()
            .map(|part| *part)
            .collect::<Vec<_>>()
            .join(".")
    }

    fn find_match(input: &str, domain: &str, candidates: Vec<&Suffix>) -> Result<Domain> {
        let d_labels: Vec<&str> = domain.split('.').rev().collect();

        let mut registrable = None;
        let mut suffix = None;
        let mut typ = None;
        let mut num_labels = 0;

        let no_possible_matches_found = candidates.is_empty();

        for candidate in candidates {
            let s_labels: Vec<&str> = candidate.rule.split('.').rev().collect();
            if s_labels.len() > d_labels.len() { continue; }
            for (i, label) in s_labels.iter().enumerate() {
                if *label == d_labels[i] || *label == "*" || label.trim_left_matches('!') == d_labels[i] {
                    if i == s_labels.len()-1 {
                        if s_labels.len() >= num_labels {
                            num_labels = s_labels.len();
                            typ = Some(candidate.typ);
                            let s_len = if label.starts_with("!") {
                                s_labels.len()-1
                            } else {
                                s_labels.len()
                            };
                            suffix = Some(Self::assemble(input, s_len));
                            if d_labels.len() > s_len {
                                let root = Self::assemble(input, s_len+1);
                                registrable = Some(root);
                            } else {
                                registrable = None;
                            }
                        }
                    }
                } else {
                    break;
                }
            }
        }

        if suffix.is_none() && d_labels.len() > 0 && no_possible_matches_found {
            suffix = Some(Self::assemble(input, 1));
            registrable = if d_labels.len() > 1 {
                Some(Self::assemble(input, 2))
            } else {
                None
            };
        }

        Ok(Domain {
            full: input.to_string(),
            typ: typ,
            suffix: suffix,
            registrable: registrable,
        })
    }

    fn to_ascii(domain: &str) -> Result<String> {
        let result = uts46::to_ascii(domain, uts46::Flags {
            use_std3_ascii_rules: false,
            transitional_processing: true,
            verify_dns_length: true,
        });
        result.map_err(|error| ErrorKind::Uts46(error).into())
    }

    fn parse(domain: &str, list: &List, check_syntax: bool) -> Result<Domain> {
        if check_syntax && !Self::has_valid_syntax(domain) {
            return Err(ErrorKind::InvalidDomain(domain.into()).into());
        }
        let input = domain.trim_right_matches('.');
        let (domain, res) = domain_to_unicode(input);
        if let Err(errors) = res {
            return Err(ErrorKind::Uts46(errors).into());
        }
        Self::find_possible_matches(&domain, list)
            .and_then(|res| Self::find_match(input, &domain, res))
    }

    /// Gets the root domain portion if any
    pub fn root(&self) -> Option<&str> {
        match self.registrable {
            Some(ref registrable) => Some(registrable),
            None => None,
        }
    }

    /// Gets the suffix if any
    pub fn suffix(&self) -> Option<&str> {
        match self.suffix {
            Some(ref suffix) => Some(suffix),
            None => None,
        }
    }

    /// Whether the domain has a private suffix
    pub fn is_private(&self) -> bool {
        match self.typ {
            Some(typ) => match typ {
                Type::Icann => false,
                Type::Private => true,
            },
            None => false,
        }
    }

    /// Whether the domain has an ICANN suffix
    pub fn is_icann(&self) -> bool {
        match self.typ {
            Some(typ) => match typ {
                Type::Icann => true,
                Type::Private => false,
            },
            None => false,
        }
    }

    /// Whether this domain's suffix is in the list
    ///
    /// If it is, this is definately a valid domain. If it's not
    /// chances are very high that this isn't a valid domain name,
    /// however, it might simply be because the suffix is new and
    /// it hasn't been added to the list yet.
    ///
    /// If you want to validate a domain name, use this as a quick
    /// check but fall back to a DNS lookup if it returns false.
    pub fn has_known_suffix(&self) -> bool {
        self.typ.is_some()
    }
}

impl fmt::Display for Domain {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.full.trim_right_matches(".").to_lowercase())
    }
}

impl DnsName {
    /// Extracts the root domain from a DNS name, if any
    pub fn domain(&self) -> Option<&Domain> {
        self.domain.as_ref()
    }
}

impl fmt::Display for DnsName {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.name.fmt(f)
    }
}
