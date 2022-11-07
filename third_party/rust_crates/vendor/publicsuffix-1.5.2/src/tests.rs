extern crate rspec;

use {List, request};
use errors::ErrorKind;
use self::rspec::context::rdescribe;

#[test]
fn list_behaviour() {
    let list = List::fetch().unwrap();

    rdescribe("the list", |ctx| {
        ctx.it("should not be empty", || {
            assert!(!list.all().is_empty());
        });

        ctx.it("should have ICANN domains", || {
            assert!(!list.icann().is_empty());
        });

        ctx.it("should have private domains", || {
            assert!(!list.private().is_empty());
        });

        ctx.it("should have at least 1000 domains", || {
            assert!(list.all().len() > 1000);
        });
    });

    rdescribe("the official test", |_| {
        let tests = "https://raw.githubusercontent.com/publicsuffix/list/master/tests/tests.txt";
        let body = request(tests).unwrap();

        let mut parse = false;

        for (i, line) in body.lines().enumerate() {
            match line {
                line if line.trim().is_empty() => { parse = true; continue; }
                line if line.starts_with("//") => { continue; }
                line => {
                    if !parse { continue; }
                    let mut test = line.split_whitespace().peekable();
                    if test.peek().is_none() {
                        continue;
                    }
                    let input = match test.next() {
                        Some("null") => "",
                        Some(res) => res,
                        None => { panic!(format!("line {} of the test file doesn't seem to be valid", i)); },
                    };
                    let (expected_root, expected_suffix) = match test.next() {
                        Some("null") => (None, None),
                        Some(root) => {
                            let suffix = {
                                let parts: Vec<&str> = root.split('.').rev().collect();
                                (&parts[..parts.len()-1]).iter().rev()
                                    .map(|part| *part)
                                    .collect::<Vec<_>>()
                                    .join(".")
                            };
                            (Some(root.to_string()), Some(suffix.to_string()))
                        },
                        None => { panic!(format!("line {} of the test file doesn't seem to be valid", i)); },
                    };
                    let (found_root, found_suffix) = match list.parse_domain(input) {
                        Ok(domain) => {
                            let found_root = match domain.root() {
                                Some(found) => Some(found.to_string()),
                                None => None,
                            };
                            let found_suffix = match domain.suffix() {
                                Some(found) => Some(found.to_string()),
                                None => None,
                            };
                            (found_root, found_suffix)
                        },
                        Err(_) => (None, None),
                    };
                    if expected_root != found_root || (expected_root.is_some() && expected_suffix != found_suffix) {
                        let msg = format!("\n\nGiven `{}`:\nWe expected root domain to be `{:?}` and suffix be `{:?}`\nBut instead, we have `{:?}` as root domain and `{:?}` as suffix.\nWe are on line {} of `test_psl.txt`.\n\n",
                                          input, expected_root, expected_suffix, found_root, found_suffix, i+1);
                        panic!(msg);
                    }
                }
            }
        }
    });

    rdescribe("a domain", |ctx| {
        ctx.it("should allow fully qualified domain names", || {
            assert!(list.parse_domain("example.com.").is_ok());
        });

        ctx.it("should not allow more than 1 trailing dot", || {
            assert!(list.parse_domain("example.com..").is_err());
            match *list.parse_domain("example.com..").unwrap_err().kind() {
                ErrorKind::InvalidDomain(ref domain) => assert_eq!(domain, "example.com.."),
                _ => assert!(false),
            }
        });

        ctx.it("should allow a single label with a single trailing dot", || {
            assert!(list.parse_domain("com.").is_ok());
        });

        ctx.it("should always have a suffix for single-label domains", || {
            let domains = vec![
                // real TLDs
                "com",
                "saarland",
                "museum.",
                // non-existant TLDs
                "localhost",
                "madeup",
                "with-dot.",
            ];
            for domain in domains {
                let res = list.parse_domain(domain).unwrap();
                assert_eq!(res.suffix(), Some(domain.trim_right_matches('.')));
                assert!(res.root().is_none());
            }
        });

        ctx.it("should have the same result with or without the trailing dot", || {
                assert_eq!(list.parse_domain("com.").unwrap(), list.parse_domain("com").unwrap());
        });

        ctx.it("should not have empty labels", || {
            assert!(list.parse_domain("exa..mple.com").is_err());
        });

        ctx.it("should not contain spaces", || {
            assert!(list.parse_domain("exa mple.com").is_err());
        });

        ctx.it("should not start with a dash", || {
            assert!(list.parse_domain("-example.com").is_err());
        });


        ctx.it("should not end with a dash", || {
            assert!(list.parse_domain("example-.com").is_err());
        });

        ctx.it("should not contain /", || {
            assert!(list.parse_domain("exa/mple.com").is_err());
        });

        ctx.it("should not have a label > 63 characters", || {
            let mut too_long_domain = String::from("a");
            for _ in 0..64 {
                too_long_domain.push_str("a");
            }
            too_long_domain.push_str(".com");
            assert!(list.parse_domain(&too_long_domain).is_err());
        });

        ctx.it("should not be an IPv4 address", || {
            assert!(list.parse_domain("127.38.53.247").is_err());
        });

        ctx.it("should not be an IPv6 address", || {
            assert!(list.parse_domain("fd79:cdcb:38cc:9dd:f686:e06d:32f3:c123").is_err());
        });

        ctx.it("should allow numbers only labels that are not the tld", || {
            assert!(list.parse_domain("127.com").is_ok());
        });

        ctx.it("should not have more than 127 labels", || {
            let mut too_many_labels_domain = String::from("a");
            for _ in 0..126 {
                too_many_labels_domain.push_str(".a");
            }
            too_many_labels_domain.push_str(".com");
            assert!(list.parse_domain(&too_many_labels_domain).is_err());
        });

        ctx.it("should not have more than 253 characters", || {
            let mut too_many_chars_domain = String::from("aaaaa");
            for _ in 0..50 {
                too_many_chars_domain.push_str(".aaaaaa");
            }
            too_many_chars_domain.push_str(".com");
            assert!(list.parse_domain(&too_many_chars_domain).is_err());
        });
    });

    rdescribe("a DNS name", |ctx| {
        ctx.it("should allow extended characters", || {
            let names = vec![
                "_tcp.example.com.",
                "_telnet._tcp.example.com.",
                "*.example.com.",
                "ex!mple.com.",
            ];
            for name in names {
                println!("{} should be valid", name);
                assert!(list.parse_dns_name(name).is_ok());
            }
        });

        ctx.it("should allow extracting the correct domain name where possible", || {
            let names = vec![
                ("_tcp.example.com.", "example.com"),
                ("_telnet._tcp.example.com.", "example.com"),
                ("*.example.com.", "example.com"),
            ];
            for (name, domain) in names {
                println!("{}'s root domain should be {}", name, domain);
                let name = list.parse_dns_name(name).unwrap();
                let root = name.domain().unwrap().root();
                assert_eq!(root, Some(domain));
            }
        });

        ctx.it("should not extract any domain where not possible", || {
            let names = vec![
                "_tcp.com.",
                "_telnet._tcp.com.",
                "*.com.",
                "ex!mple.com.",
            ];
            for name in names {
                println!("{} should not have any root domain", name);
                let name = list.parse_dns_name(name).unwrap();
                assert!(name.domain().is_none());
            }
        });

        ctx.it("should not allow more than 1 trailing dot", || {
            assert!(list.parse_dns_name("example.com..").is_err());
            match *list.parse_dns_name("example.com..").unwrap_err().kind() {
                ErrorKind::InvalidDomain(ref domain) => assert_eq!(domain, "example.com.."),
                _ => assert!(false),
            }
        });
    });

    rdescribe("a host", |ctx| {
        ctx.it("can be an IPv4 address", || {
            assert!(list.parse_host("127.38.53.247").is_ok());
        });

        ctx.it("can be an IPv6 address", || {
            assert!(list.parse_host("fd79:cdcb:38cc:9dd:f686:e06d:32f3:c123").is_ok());
        });

        ctx.it("can be a domain name", || {
            assert!(list.parse_host("example.com").is_ok());
        });

        ctx.it("cannot be neither an IP address nor a domain name", || {
            assert!(list.parse_host("23.56").is_err());
        });

        ctx.it("an IPv4 address should parse into an IP object", || {
            assert!(list.parse_host("127.38.53.247").unwrap().is_ip());
        });

        ctx.it("an IPv6 address should parse into an IP object", || {
            assert!(list.parse_host("fd79:cdcb:38cc:9dd:f686:e06d:32f3:c123").unwrap().is_ip());
        });

        ctx.it("a domain name should parse into a domain object", || {
            assert!(list.parse_host("example.com").unwrap().is_domain());
        });

        ctx.it("can be parsed from a URL with a domain as hostname", || {
            assert!(list.parse_url("https://publicsuffix.org/list/").unwrap().is_domain());
        });

        ctx.it("can be parsed from a URL with an IP address as hostname", || {
            assert!(list.parse_url("https://127.38.53.247:8080/list/").unwrap().is_ip());
        });

        ctx.it("can be parsed from a URL using `parse_str`", || {
            assert!(list.parse_str("https://127.38.53.247:8080/list/").unwrap().is_ip());
        });

        ctx.it("can be parsed from a non-URL using `parse_str`", || {
            assert!(list.parse_str("example.com").unwrap().is_domain());
        });
    });

    rdescribe("a parsed email", |ctx| {
        ctx.it("should allow valid email addresses", || {
            let emails = vec![
                "prettyandsimple@example.com",
                "very.common@example.com",
                "disposable.style.email.with+symbol@example.com",
                "other.email-with-dash@example.com",
                "x@example.com",
                "example-indeed@strange-example.com",
                "#!$%&'*+-/=?^_`{}|~@example.org",
                "example@s.solutions",
                "user@[fd79:cdcb:38cc:9dd:f686:e06d:32f3:c123]",
                r#""Abc\@def"@example.com"#,
                r#""Fred Bloggs"@example.com"#,
                r#""Joe\\Blow"@example.com"#,
                r#""Abc@def"@example.com"#,
                r#"customer/department=shipping@example.com"#,
                "$A12345@example.com",
                "!def!xyz%abc@example.com",
                "_somename@example.com",
            ];
            for email in emails {
                println!("{} should be valid", email);
                assert!(list.parse_email(email).is_ok());
            }
        });

        ctx.it("should reject invalid email addresses", || {
            let emails = vec![
                "Abc.example.com",
                "A@b@c@example.com",
                r#"a"b(c)d,e:f;g<h>i[j\k]l@example.com"#,
                r#""just"not"right@example.com"#,
                r#"this is"not\allowed@example.com"#,
                r#"this\ still\"not\\allowed@example.com"#,
                "1234567890123456789012345678901234567890123456789012345678901234+x@example.com",
                "john..doe@example.com",
                "john.doe@example..com",
                " prettyandsimple@example.com",
                "prettyandsimple@example.com ",
            ];
            for email in emails {
                println!("{} should not be valid", email);
                assert!(list.parse_email(email).is_err());
            }
        });

        ctx.it("should allow parsing emails as str", || {
            assert!(list.parse_str("prettyandsimple@example.com").unwrap().is_domain());
        });

        ctx.it("should allow parsing emails as URL", || {
            assert!(list.parse_url("mailto://prettyandsimple@example.com").unwrap().is_domain());
        });

        ctx.it("should allow parsing IDN email addresses", || {
            let emails = vec![
                r#"Pelé@example.com"#,
                r#"δοκιμή@παράδειγμα.δοκιμή"#,
                r#"我買@屋企.香港"#,
                r#"甲斐@黒川.日本"#,
                r#"чебурашка@ящик-с-апельсинами.рф"#,
                r#"संपर्क@डाटामेल.भारत"#,
                r#"用户@例子.广告"#,
            ];
            for email in emails {
                println!("{} should be valid", email);
                assert!(list.parse_email(email).is_ok());
            }
        });
    });
}
