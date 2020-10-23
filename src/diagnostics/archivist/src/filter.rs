// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use regex::Regex;
use regex::RegexSet;

pub fn filter_line(line: &str) -> String {
    let regex_set =  RegexSet::new(&[
        // Email stub alice@website.tld
        r"[a-zA-Z0-9]*@[a-zA-Z0-9]*\.[a-zA-Z]*",
        // IPv4 Address
        r"((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])",
        // IPv6
        r"(?:[a-fA-F0-9]{1,4}:){7}[a-fA-F0-9]{1,4}",
        // uuid
        r"[0-9a-fA-F]{8}\b-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-\b[0-9a-fA-F]{12}",
        // mac address
        r"([0-9a-fA-F]{1,2}([\.:-])){5}[0-9a-fA-F]{1,2}",
        ]).unwrap();

    let matches: Vec<_> = regex_set
        .matches(line)
        .into_iter()
        .map(|match_idx| &regex_set.patterns()[match_idx])
        .collect();
    let mut return_value = line.to_owned(); // Take ownership
    for case in matches.iter() {
        let ref_return_value =
            Regex::new(case).expect("Regex failed").replace_all(&return_value, "<REDACTED>");
        return_value = ref_return_value.into_owned();
    }
    return_value
}

#[cfg(test)]
mod test {
    use super::*;
    #[test]
    fn test_emailfilter() {
        assert_eq!("Email: <REDACTED>".to_string(), filter_line("Email: alice@website.tld"))
    }
    #[test]
    fn test_ipv4filter() {
        assert_eq!("IPv4: <REDACTED>".to_string(), filter_line("IPv4: 8.8.8.8"))
    }
    #[test]
    fn test_ipv6filter() {
        assert_eq!("IPv6: <REDACTED>".to_string(), filter_line("IPv6: 2001:503:eEa3:0:0:0:0:30"))
    }
    #[test]
    fn test_uuidfilter() {
        assert_eq!(
            "UUID: <REDACTED>".to_string(),
            filter_line("UUID: ddd0fA34-1016-11eb-adc1-0242ac120002")
        )
    }
    #[test]
    fn test_macaddressfilter() {
        assert_eq!(
            "MAC address: <REDACTED>".to_string(),
            filter_line("MAC address: 00:0a:95:9F:68:16")
        )
    }
    #[test]
    fn test_combinedfilter() {
        assert_eq!(
            "Combined: Email <REDACTED>, IPv4 <REDACTED>".to_string(),
            filter_line("Combined: Email alice@website.tld, IPv4 8.8.8.8")
        )
    }
}
