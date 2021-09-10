// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;
use regex::Regex;

pub struct PBusDeviceDesc {
    pub pid: String,
    pub did: String,
    pub vid: String,
    pub instance_id: String,
}

pub struct CompositeDeviceDesc {
    pub device_name: String,
    pub primary_node: String,
    pub pbus_desc: Option<PBusDeviceDesc>,
}

fn get_pdev_int_val(pdev_var_name: &str, val_name: &str, contents: &str) -> String {
    let val_regex =
        Regex::new(&format!("{}.{} = {}", pdev_var_name, val_name, r#"([^;]*)"#)).unwrap();
    match val_regex.captures(contents) {
        Some(cap) => cap.get(1).unwrap().as_str(),
        None => "0",
    }
    .to_string()
}

// Try to get the device description from pbus_dev_t and CompositeDeviceAdd().
// This function checks if a pbus_dev_t variable is defined and then tries to extract
// the values from it.
fn get_pdev_device_desc(contents: &str) -> Result<Option<CompositeDeviceDesc>, &'static str> {
    let pdev_regex = Regex::new(r"pbus_dev_t (([A-Za-z0-9_])*) = \{\}").unwrap();
    if !pdev_regex.is_match(contents) {
        return Ok(None);
    }

    let pdev_capture = pdev_regex.captures(contents).unwrap();
    let pdev_var_name = pdev_capture.get(1).unwrap().as_str();

    let device_name_regex =
        Regex::new(&format!("{}.name = {}", pdev_var_name, r#""([^"]*)""#)).unwrap();
    let device_name =
        device_name_regex.captures(contents).unwrap().get(1).unwrap().as_str().to_string();

    let vid = get_pdev_int_val(pdev_var_name, "vid", contents);
    let pid = get_pdev_int_val(pdev_var_name, "pid", contents);
    let did = get_pdev_int_val(pdev_var_name, "did", contents);
    let instance_id = get_pdev_int_val(pdev_var_name, "instance_id", contents);

    // Retrieve the device name and primary node name from CompositeDeviceAdd(). If
    // the primary node is set to nullptr, the primary node is "pdev".
    let composite_add_regex =
        Regex::new(r#"CompositeDeviceAdd\([^,]*,\s*[^,]*,\s*[^,]*,\s*((?P<null_val>nullptr)|"(?P<node_name>[^"]*)")\)"#)
            .unwrap();
    if !composite_add_regex.is_match(&contents) {
        return Ok(None);
    }

    let composite_add_capture = composite_add_regex.captures(contents).unwrap();
    let primary_node = if composite_add_capture.name("null_val").is_some() {
        "pdev".to_string()
    } else {
        capture_name(&composite_add_capture, "node_name")?
    };

    Ok(Some(CompositeDeviceDesc {
        device_name: device_name,
        primary_node: primary_node,
        pbus_desc: Some(PBusDeviceDesc { vid, pid, did, instance_id }),
    }))
}

fn get_composite_device_desc_t(
    contents: &str,
) -> Result<Option<CompositeDeviceDesc>, &'static str> {
    // Check for a composite_device_desc_t match.
    let comp_dev_desc_regex =
        Regex::new(r"composite_device_desc_t (([A-Za-z0-9_])*) = \{").unwrap();
    if !comp_dev_desc_regex.is_match(contents) {
        return Ok(None);
    }

    // Get primary node name from .primary_fragment = "{primary_node}",
    let primary_fragment_regex = Regex::new(r#".primary_fragment = "(?P<primary>[^"]*)""#).unwrap();
    let primary_cap = primary_fragment_regex.captures(contents).unwrap();
    let primary_node = primary_cap.name("primary").unwrap().as_str().to_string();

    let ddk_add_regex = Regex::new(r#"DdkAddComposite\("(?P<name>[^"]*)","#).unwrap();
    let composite_add_regex =
        Regex::new(r#"device_add_composite\([^,]*,\s*"(?P<name>[^"]*)",\s*[^,]*\)"#).unwrap();

    // Try to get the device name from DdkAddComposite() or device_add_composite().
    let device_name = if let Some(ddk_cap) = ddk_add_regex.captures(contents) {
        capture_name(&ddk_cap, "name")?
    } else if let Some(composite_add_cap) = composite_add_regex.captures(contents) {
        capture_name(&composite_add_cap, "name")?
    } else {
        return Err("Unable to find DdkAddComposite() or device_add_composite()");
    };

    Ok(Some(CompositeDeviceDesc {
        device_name: device_name,
        primary_node: primary_node,
        pbus_desc: None,
    }))
}

// Try to get the composite device desc from pbus_dev_t. If one isn't defined, then
// this function will try to get one from a composite_device_desc_t value.
pub fn get_device_desc(contents: &str) -> Result<CompositeDeviceDesc, &'static str> {
    if let Some(pdev_desc) = get_pdev_device_desc(contents)? {
        return Ok(pdev_desc);
    }

    if let Some(comp_dev_desc) = get_composite_device_desc_t(contents)? {
        return Ok(comp_dev_desc);
    }

    return Err("Unable to find composite_device_desc_t or pbus_dev_t value");
}
