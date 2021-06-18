// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::collections::BTreeMap;
use url::form_urlencoded;

pub use crate::env_info::*;

const GA_PROPERTY_KEY: &str = "tid";

const GA_CLIENT_KEY: &str = "cid";

// TODO(fxb/71579): match zxdb by changing category and action for analytics commands
// const GA_EVENT_CATEGORY_ANALYTICS: &str = "analytics";
// const GA_EVENT_CATEGORY_ANALYTICS_ACTION_ENABLE: &str = "manual-enable";
// const GA_EVENT_CATEGORY_ANALYTICS_ACTION_DISABLE: &str = "disable";

const GA_EVENT_CATEGORY_KEY: &str = "ec";
const GA_EVENT_CATEGORY_DEFAULT: &str = "general";
const GA_EVENT_ACTION_KEY: &str = "ea";
const GA_EVENT_LABEL_KEY: &str = "el";

const GA_TIMING_CATEGORY_KEY: &str = "utc";
const GA_TIMING_CATEGORY_DEFAULT: &str = "general";
const GA_TIMING_TIMING_KEY: &str = "utt";
const GA_TIMING_VARIABLE_KEY: &str = "utv";
const GA_TIMING_LABEL_KEY: &str = "utl";

const GA_DATA_TYPE_KEY: &str = "t";
const GA_DATA_TYPE_EVENT_KEY: &str = "event";
const GA_DATA_TYPE_TIMING_KEY: &str = "timing";
const GA_DATA_TYPE_EXCEPTION_KEY: &str = "exception";

const GA_PROTOCOL_KEY: &str = "v";
const GA_PROTOCOL_VAL: &str = "1";

const GA_APP_NAME_KEY: &str = "an";
const GA_APP_VERSION_KEY: &str = "av";
const GA_APP_VERSION_DEFAULT: &str = "unknown";

const GA_CUSTOM_DIMENSION_1_KEY: &str = "cd1";

const GA_EXCEPTION_DESCRIPTION_KEY: &str = "exd";
const GA_EXCEPTION_FATAL_KEY: &str = "exf";

/// Produces http encoded parameter string to send to the analytics
/// service.
pub fn make_body_with_hash(
    app_name: &str,
    app_version: Option<&str>,
    ga_property_id: &str,
    category: Option<&str>,
    action: Option<&str>,
    label: Option<&str>,
    custom_dimensions: BTreeMap<&str, String>,
    uuid: String,
) -> String {
    let uname = os_and_release_desc();

    let mut params = BTreeMap::new();
    params.insert(GA_PROTOCOL_KEY, GA_PROTOCOL_VAL);
    params.insert(GA_PROPERTY_KEY, ga_property_id);
    params.insert(GA_CLIENT_KEY, &uuid);
    params.insert(GA_DATA_TYPE_KEY, GA_DATA_TYPE_EVENT_KEY);
    params.insert(GA_APP_NAME_KEY, app_name);
    params.insert(
        GA_APP_VERSION_KEY,
        match app_version {
            Some(ver) => ver,
            None => GA_APP_VERSION_DEFAULT,
        },
    );
    params.insert(
        GA_EVENT_CATEGORY_KEY,
        match category {
            Some(value) => value,
            None => GA_EVENT_CATEGORY_DEFAULT,
        },
    );
    insert_if_present(GA_EVENT_ACTION_KEY, &mut params, action);
    insert_if_present(GA_EVENT_LABEL_KEY, &mut params, label);

    for (&key, value) in custom_dimensions.iter() {
        params.insert(key, value);
    }
    params.insert(GA_CUSTOM_DIMENSION_1_KEY, &uname);

    let body = to_kv_post_body(&params);
    //println!("body = {}", &body);
    body
}

pub fn make_timing_body_with_hash(
    app_name: &str,
    app_version: Option<&str>,
    ga_property_id: &str,
    category: Option<&str>,
    time: String,
    variable: Option<&str>,
    label: Option<&str>,
    custom_dimensions: BTreeMap<&str, String>,
    uuid: String,
) -> String {
    let uname = os_and_release_desc();

    let mut params = BTreeMap::new();
    params.insert(GA_PROTOCOL_KEY, GA_PROTOCOL_VAL);
    params.insert(GA_PROPERTY_KEY, ga_property_id);
    params.insert(GA_CLIENT_KEY, &uuid);
    params.insert(GA_DATA_TYPE_KEY, GA_DATA_TYPE_TIMING_KEY);
    params.insert(GA_APP_NAME_KEY, app_name);
    params.insert(
        GA_APP_VERSION_KEY,
        match app_version {
            Some(ver) => ver,
            None => GA_APP_VERSION_DEFAULT,
        },
    );
    params.insert(
        GA_TIMING_CATEGORY_KEY,
        match category {
            Some(value) => value,
            None => GA_TIMING_CATEGORY_DEFAULT,
        },
    );
    params.insert(GA_TIMING_TIMING_KEY, &time);
    insert_if_present(GA_TIMING_LABEL_KEY, &mut params, label);
    insert_if_present_or(GA_TIMING_VARIABLE_KEY, &mut params, variable, "");

    for (&key, value) in custom_dimensions.iter() {
        params.insert(key, value);
    }
    params.insert(GA_CUSTOM_DIMENSION_1_KEY, &uname);

    let body = to_kv_post_body(&params);
    //println!("body = {}", &body);
    body
}

fn insert_if_present<'a>(
    key: &'a str,
    params: &mut BTreeMap<&'a str, &'a str>,
    value: Option<&'a str>,
) {
    match value {
        Some(val) => {
            if !val.is_empty() {
                params.insert(key, val);
            }
        }
        None => (),
    };
}

fn insert_if_present_or<'a>(
    key: &'a str,
    params: &mut BTreeMap<&'a str, &'a str>,
    value: Option<&'a str>,
    default: &'a str,
) {
    match value {
        Some(val) => params.insert(key, val),
        None => params.insert(key, default),
    };
}

fn to_kv_post_body(params: &BTreeMap<&str, &str>) -> String {
    let mut serializer = form_urlencoded::Serializer::new(String::new());
    serializer.extend_pairs(params.iter());
    serializer.finish()
}

pub fn make_crash_body_with_hash(
    app_name: &str,
    app_version: Option<&str>,
    ga_property_id: &str,
    description: &str,
    fatal: Option<&bool>,
    custom_dimensions: BTreeMap<&str, String>,
    uuid: String,
) -> String {
    let uname = os_and_release_desc();

    let mut params = BTreeMap::new();
    params.insert(GA_PROTOCOL_KEY, GA_PROTOCOL_VAL);
    params.insert(GA_PROPERTY_KEY, ga_property_id);
    params.insert(GA_CLIENT_KEY, &uuid);
    params.insert(GA_DATA_TYPE_KEY, GA_DATA_TYPE_EXCEPTION_KEY);
    params.insert(GA_APP_NAME_KEY, app_name);
    params.insert(
        GA_APP_VERSION_KEY,
        match app_version {
            Some(ver) => ver,
            None => GA_APP_VERSION_DEFAULT,
        },
    );

    params.insert(GA_EXCEPTION_DESCRIPTION_KEY, &description);

    params.insert(
        GA_EXCEPTION_FATAL_KEY,
        match fatal {
            Some(true) => "1",
            _ => "0",
        },
    );

    for (&key, value) in custom_dimensions.iter() {
        params.insert(key, value);
    }
    params.insert(GA_CUSTOM_DIMENSION_1_KEY, &uname);

    let body = to_kv_post_body(&params);
    //println!("body = {}", &body);
    body
}

#[cfg(test)]
mod test {
    use super::*;

    pub const GA_PROPERTY_ID: &str = "UA-175659118-1";

    #[test]
    fn make_post_body() {
        let args = "config analytics enable";
        let args_encoded = "config+analytics+enable";
        let app_name = "ffx";
        let app_version = "1";
        let uname = os_and_release_desc().replace(" ", "+");
        let cid = "test";
        let expected = format!(
            "an={}&av={}&cd1={}&cid={}&ea={}&ec=general&el={}&t=event&tid={}&v=1",
            &app_name, &app_version, &uname, &cid, &args_encoded, &args_encoded, GA_PROPERTY_ID
        );
        assert_eq!(
            expected,
            make_body_with_hash(
                app_name,
                Some(app_version),
                GA_PROPERTY_ID,
                None,
                Some(args),
                Some(args),
                BTreeMap::new(),
                String::from("test")
            )
        );
    }

    #[test]
    fn make_post_body_with_labels() {
        let args = "config analytics enable";
        let args_encoded = "config+analytics+enable";
        let labels = "labels";
        let app_name = "ffx";
        let app_version = "1";
        let uname = os_and_release_desc().replace(" ", "+");
        let cid = "test";
        let expected = format!(
            "an={}&av={}&cd1={}&cid={}&ea={}&ec=general&el={}&t=event&tid={}&v=1",
            &app_name, &app_version, &uname, &cid, &args_encoded, &labels, GA_PROPERTY_ID
        );
        assert_eq!(
            expected,
            make_body_with_hash(
                app_name,
                Some(app_version),
                GA_PROPERTY_ID,
                None,
                Some(args),
                Some(labels),
                BTreeMap::new(),
                String::from("test")
            )
        );
    }

    #[test]
    fn make_post_body_with_custom_dimensions() {
        let args = "config analytics enable";
        let args_encoded = "config+analytics+enable";
        let labels = "labels";
        let app_name = "ffx";
        let app_version = "1";
        let uname = os_and_release_desc().replace(" ", "+");
        let cd3_val = "foo".to_string();
        let cid = "test";
        let expected = format!(
            "an={}&av={}&cd1={}&cd3={}&cid={}&ea={}&ec=general&el={}&t=event&tid={}&v=1",
            &app_name, &app_version, &uname, &cd3_val, &cid, &args_encoded, &labels, GA_PROPERTY_ID
        );
        let mut custom_dimensions = BTreeMap::new();
        custom_dimensions.insert("cd3", cd3_val);
        assert_eq!(
            expected,
            make_body_with_hash(
                app_name,
                Some(app_version),
                GA_PROPERTY_ID,
                None,
                Some(args),
                Some(labels),
                custom_dimensions,
                String::from("test")
            )
        );
    }

    #[test]
    fn make_post_body_for_crash() {
        let app_name = "ffx";
        let app_version = "1";
        let uname = os_and_release_desc().replace(" ", "+");
        let cid = "test";
        let description_encoded = "Exception+foo";
        let fatal = false;
        let expected = format!(
            "an={}&av={}&cd1={}&cid={}&exd={}&exf={}&t=exception&tid={}&v=1",
            &app_name, &app_version, &uname, &cid, &description_encoded, "0", GA_PROPERTY_ID
        );
        assert_eq!(
            expected,
            make_crash_body_with_hash(
                app_name,
                Some(app_version),
                GA_PROPERTY_ID,
                "Exception foo",
                Some(&fatal),
                BTreeMap::new(),
                String::from("test")
            )
        );
    }
    #[test]
    fn insert_if_present_some() {
        let mut map2 = BTreeMap::new();
        let val_str = "property1";
        let val = Some(val_str);
        let key = "key1";
        insert_if_present(key, &mut map2, val);
        assert_eq!(val_str, *map2.get(key).unwrap());
    }

    #[test]
    fn insert_if_present_none() {
        let mut map2 = BTreeMap::new();
        let val = None;
        let key = "key1";
        insert_if_present(key, &mut map2, val);
        assert_eq!(None, map2.get(key));
    }

    #[test]
    fn insert_if_present_empty() {
        let mut map2 = BTreeMap::new();
        let val = "";
        let key = "key1";
        insert_if_present(key, &mut map2, Some(val));
        assert_eq!(None, map2.get(key));
    }

    #[test]
    fn insert_if_present_or_with_value() {
        let mut map2 = BTreeMap::new();
        let val_str = "property1";
        let val = Some(val_str);
        let key = "key1";
        insert_if_present_or(key, &mut map2, val, "default");
        assert_eq!(val_str, *map2.get(key).unwrap());
    }

    #[test]
    fn insert_if_present_or_with_default() {
        let mut map2 = BTreeMap::new();
        let val = None;
        let key = "key1";
        insert_if_present_or(key, &mut map2, val, "default");
        assert_eq!("default", *map2.get(key).unwrap());
    }

    #[test]
    fn make_post_body_for_timing_event() {}
}
