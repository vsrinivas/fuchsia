// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::protocol::Cohort;
use pretty_assertions::assert_eq;
use serde_json::json;

#[test]
fn test_invalid_json() {
    assert!(parse_json_response(b"invalid_json").is_err());
}

#[test]
fn test_empty_json() {
    assert!(parse_json_response(b"{}").is_err());
}

#[test]
fn test_minimal() {
    let json = br#"
{"response":{
 "server":"prod",
 "protocol":"3.0",
 "app":[
  {
   "appid":"{00000000-0000-0000-0000-000000000001}",
   "status":"ok"
  }
 ]
}}"#;
    let result = parse_json_response(json);
    assert!(result.is_ok());
    let response = result.unwrap();

    let expected = Response {
        protocol_version: "3.0".to_string(),
        server: "prod".to_string(),
        daystart: None,
        apps: vec![App {
            id: "{00000000-0000-0000-0000-000000000001}".to_string(),
            ..App::default()
        }],
    };
    assert_eq!(response, expected);
}

#[test]
fn test_all_fields() {
    let json = br##"
{"response":{
 "messagetype":"response",
 "server":"test",
 "protocol":"3.0",
 "daystart":{"elapsed_seconds":54956,"elapsed_days":4242},
 "app":[
  {
   "appid":"{00000000-0000-0000-0000-000000000001}",
   "cohort":"",
   "status":"ok",
   "cohorthint":"",
   "cohortname":"",
   "updatecheck":{"status":"noupdate","info":"no update for you"},
   "ping":{"status":"ok"},
   "event":[
    {"status":"ok"},
    {"status":"ok"}
   ],
   "data":{
    "name":"install",
    "index":"default",
    "status":"ok",
    "#text":
     "{\n  \"distribution\" : {\n    \"some_flag\" : true\n  }\n}\n"}
   },
   {
    "appid":"{11111111-1111-1111-1111-111111111111}",
    "cohort":"",
    "status":"ok",
    "cohortname":"",
    "updatecheck":{
     "status":"ok",
     "urls":{
      "url":[
       {"codebase":"http://url/base/"},
       {"codebase":"https://url/base/"}
      ]
     },
     "manifest":{
      "version":"1.3.33.17",
      "actions":{
       "action":[
        {"run":"update_package",
          "arguments":"/update","event":"update"},
        {"event":"postinstall"},
        {"run":"ChromeRecovery.crx"}
       ]
      },
      "packages":{
       "package":[
        {
         "hash_sha256":
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         "size":2000000,
         "name":"update_package",
         "fp":
          "1.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         "required":true,
         "hash":"qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"
        }
       ]
      }
     }
    },
    "ping":{"status":"ok"},
    "event":[
     {"status":"ok"}
    ]
   }
 ]
}}"##;
    let result = parse_json_response(json);
    assert!(result.is_ok());
    let response = result.unwrap();

    let expected = Response {
        protocol_version: "3.0".to_string(),
        server: "test".to_string(),
        daystart: Some(DayStart { elapsed_seconds: Some(54956), elapsed_days: Some(4242) }),
        apps: vec![
            App {
                id: "{00000000-0000-0000-0000-000000000001}".to_string(),
                status: OmahaStatus::Ok,
                cohort: Cohort {
                    id: Some("".to_string()),
                    hint: Some("".to_string()),
                    name: Some("".to_string())
                },
                ping: Some(Ping { status: OmahaStatus::Ok }),
                update_check: Some(UpdateCheck {
                    status: OmahaStatus::NoUpdate,
                    info: Some("no update for you".to_string()),
                    urls: None,
                    manifest: None,
                }),
                events: Some(vec![
                    Event{ status: OmahaStatus::Ok },
                    Event{ status: OmahaStatus::Ok },
                ]),
                extra_attributes: json!({"data":{
                                            "name":"install",
                                            "index":"default",
                                            "status":"ok",
                                            "#text":"{\n  \"distribution\" : {\n    \"some_flag\" : true\n  }\n}\n"
                                        }})
                                    .as_object()
                                    .unwrap()
                                    .to_owned(),
            },
            App {
                id: "{11111111-1111-1111-1111-111111111111}".to_string(),
                status: OmahaStatus::Ok,
                cohort: Cohort {
                    id: Some("".to_string()),
                    hint: None,
                    name: Some("".to_string())
                },
                ping: Some(Ping { status: OmahaStatus::Ok }),
                update_check: Some(UpdateCheck {
                    status: OmahaStatus::Ok,
                    info: None,
                    urls: Some(URLs::new(vec![
                        "http://url/base/".to_string(),
                        "https://url/base/".to_string()
                    ])),
                    manifest: Some(Manifest {
                        version: "1.3.33.17".to_string(),
                        actions: Actions{action:vec![
                            Action {
                                event: Some("update".to_string()),
                                run: Some("update_package".to_string()),
                                extra_attributes: json!({"arguments":"/update"})
                                    .as_object()
                                    .unwrap()
                                    .to_owned(),
                            },
                            Action {
                                event: Some("postinstall".to_string()),
                                run: None,
                                extra_attributes: Map::new(),
                            },
                            Action {
                                event: None,
                                run: Some("ChromeRecovery.crx".to_string()),
                                extra_attributes: Map::new(),
                            },
                        ]},
                        packages: Packages{package: vec![Package{
                            name: "update_package".to_string(),
                            required: true,
                            size: Some(2000000),
                            hash: Some("qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq".to_string()),
                            hash_sha256:
                                Some("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".to_string()),
                            fingerprint:
                                "1.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".to_string(),
                            extra_attributes: Map::new(),
                        }]},
                    }),
                }),
                events: Some(vec![Event{status: OmahaStatus::Ok}]),
                extra_attributes: Map::new(),
            },
        ],
    };
    assert_eq!(response, expected);
}

#[test]
fn test_new_cohort() {
    let json = br#"
{"response":{
 "server":"prod",
 "protocol":"3.0",
 "app":[
  {
   "appid":"{00000000-0000-0000-0000-000000000001}",
   "cohort":"1:3:",
   "status":"ok",
   "cohortname":"stable",
   "updatecheck":{"status":"noupdate"},
   "ping":{"status":"ok"}
  }
 ]
}}"#;
    let result = parse_json_response(json);
    assert!(result.is_ok());
    let response = result.unwrap();

    let expected = Response {
        protocol_version: "3.0".to_string(),
        server: "prod".to_string(),
        daystart: None,
        apps: vec![App {
            id: "{00000000-0000-0000-0000-000000000001}".to_string(),
            status: OmahaStatus::Ok,
            cohort: Cohort {
                id: Some("1:3:".to_string()),
                hint: None,
                name: Some("stable".to_string()),
            },
            ping: Some(Ping { status: OmahaStatus::Ok }),
            update_check: Some(UpdateCheck::no_update()),
            ..App::default()
        }],
    };
    assert_eq!(response, expected);
}

#[test]
fn test_unknown_app_id() {
    let json = br#"
{"response":{
 "server":"prod",
 "protocol":"3.0",
 "app":[
  {
   "appid":"{00000000-0000-0000-0000-000000000001}",
   "status": "error-unknownApplication"
  }
 ]
}}"#;
    let result = parse_json_response(json);
    assert!(result.is_ok());
    let response = result.unwrap();

    let expected = Response {
        protocol_version: "3.0".to_string(),
        server: "prod".to_string(),
        daystart: None,
        apps: vec![App {
            id: "{00000000-0000-0000-0000-000000000001}".to_string(),
            status: OmahaStatus::Error("error-unknownApplication".to_string()),
            ..App::default()
        }],
    };
    assert_eq!(response, expected);
}

#[test]
fn test_single_url() {
    let json = br#"
{"response":{
 "server":"prod",
 "protocol":"3.0",
 "app":[
   {
    "appid":"single-url-appid",
    "status":"ok",
    "updatecheck":{
     "status":"ok",
     "urls":{
      "url":[
       {"codebase":"http://url/base/"}
      ]
     },
     "manifest":{
      "version":"1.0",
      "actions":{
       "action":[
        {"event":"install", "run":"full.payload"}
       ]
      },
      "packages":{
       "package":[
        {
         "hash_sha256":
          "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
         "size":100000000,
         "name":"full.payload",
         "fp":
          "1.bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
         "required":true
        }
       ]
      }
     }
    }
   }
 ]
}}"#;
    let result = parse_json_response(json);
    assert!(result.is_ok());
    let response = result.unwrap();

    let expected = Response {
        protocol_version: "3.0".to_string(),
        server: "prod".to_string(),
        daystart: None,
        apps: vec![App {
            id: "single-url-appid".to_string(),
            status: OmahaStatus::Ok,
            update_check: Some(UpdateCheck {
                status: OmahaStatus::Ok,
                info: None,
                urls: Some(URLs::new(vec!["http://url/base/".to_string()])),
                manifest: Some(Manifest {
                    version: "1.0".to_string(),
                    actions: Actions {
                        action: vec![Action {
                            event: Some("install".to_string()),
                            run: Some("full.payload".to_string()),
                            extra_attributes: Map::new(),
                        }],
                    },
                    packages: Packages {
                        package: vec![Package {
                            name: "full.payload".to_string(),
                            required: true,
                            size: Some(100000000),
                            hash: None,
                            hash_sha256: Some(
                                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                                    .to_string(),
                            ),
                            fingerprint:
                                "1.bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
                                    .to_string(),
                            extra_attributes: Map::new(),
                        }],
                    },
                }),
            }),
            ..App::default()
        }],
    };
    assert_eq!(response, expected);
}

#[test]
fn test_no_update() {
    let json = br#"
{"response":{
 "server":"prod",
 "protocol":"3.0",
 "app":[
  {
   "appid":"no-update-appid",
   "status":"ok",
   "updatecheck":{
    "status":"noupdate"
   }
  }
 ]
}}"#;
    let result = parse_json_response(json);
    assert!(result.is_ok());
    let response = result.unwrap();

    let expected = Response {
        protocol_version: "3.0".to_string(),
        server: "prod".to_string(),
        daystart: None,
        apps: vec![App {
            id: "no-update-appid".to_string(),
            update_check: Some(UpdateCheck::no_update()),
            ..App::default()
        }],
    };
    assert_eq!(response, expected);
}

#[test]
fn test_unsupported_protocol_version() {
    let json = br#"
{"response":{
 "server":"prod",
 "protocol":"2.0",
 "app":[
  {
   "appid":"{00000000-0000-0000-0000-000000000001}",
   "status":"ok"
  }
 ]
}}"#;
    let result = parse_json_response(json);
    assert!(result.is_ok());
    let response = result.unwrap();

    let expected = Response {
        protocol_version: "2.0".to_string(),
        server: "prod".to_string(),
        daystart: None,
        apps: vec![App {
            id: "{00000000-0000-0000-0000-000000000001}".to_string(),
            ..App::default()
        }],
    };
    assert_eq!(response, expected);
}

#[test]
fn test_missing_app() {
    let json = br#"
{"response":{
 "server":"prod",
 "protocol":"3.0"
}}"#;
    assert!(parse_json_response(json).is_err());
}

#[test]
fn test_no_safe_json() {
    let valid_json = json!({"this":["is", "valid", "json"]});
    let valid_json_bytes =
        serde_json::to_vec_pretty(&valid_json).expect("Unable to serialize JSON to string");

    let parse_result: serde_json::Result<serde_json::Value> = parse_safe_json(&valid_json_bytes);
    let parsed_json = parse_result.expect("Unable to parse valid JSON");

    assert_eq!(parsed_json, valid_json);
}

#[test]
fn test_safe_json() {
    let valid_json = json!({"this":["is", "valid", "json"]});
    let mut safe_json = b")]}'\n".to_vec();

    serde_json::to_writer(&mut safe_json, &valid_json)
        .expect("Unable to construct 'safe' test JSON");

    let parsed_json: serde_json::Value =
        parse_safe_json(&safe_json).expect("Unable to parse 'made safe' JSON");

    assert_eq!(parsed_json, valid_json);
}
