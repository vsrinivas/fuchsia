// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;
use crate::composite_device_desc::*;
use crate::library::*;
use regex::Regex;
use std::collections::{HashMap, HashSet};
use std::convert::TryFrom;
use std::fmt::Write;
use std::fs::OpenOptions;
use std::io::Read;
use std::path::PathBuf;

#[derive(Debug, PartialEq)]
enum Condition {
    Always,
    Equals,
    NotEquals,
    GreaterThan,
    LessThan,
    GreaterThanOrEqual,
    LessThanOrEqual,
}

impl TryFrom<&str> for Condition {
    type Error = &'static str;

    fn try_from(input: &str) -> Result<Self, Self::Error> {
        match input {
            "AL" => Ok(Condition::Always),
            "EQ" => Ok(Condition::Equals),
            "NE" => Ok(Condition::NotEquals),
            "GT" => Ok(Condition::GreaterThan),
            "LT" => Ok(Condition::LessThan),
            "GE" => Ok(Condition::GreaterThanOrEqual),
            "LE" => Ok(Condition::LessThanOrEqual),
            _ => Err("Unrecognised condition"),
        }
    }
}

pub struct NodeData {
    pub name: String,
    pub bind_rules: String,
}

pub struct CompositeDeviceData {
    pub libraries: HashSet<Library>,
    pub nodes: Vec<NodeData>,
    pub desc: CompositeDeviceDesc,
}

struct CompositeBindRules {
    libraries: HashSet<Library>,
    match_insts: HashMap<String, String>,
}

impl CompositeBindRules {
    pub fn new() -> Self {
        CompositeBindRules { libraries: HashSet::new(), match_insts: HashMap::new() }
    }

    pub fn add_match_insts(&mut self, insts: &str) -> Result<(), &'static str> {
        let op_regex = Regex::new(r"BI_[A-Z_]*\([^\)]*\)").unwrap();
        let abort_regex = Regex::new(r"BI_ABORT\(\)").unwrap();
        let abort_if_regex = Regex::new(r"BI_ABORT_IF\(([A-Z][A-Z]),([^,]*),([^\)]*)\)").unwrap();
        let match_if_regex = Regex::new(r"BI_MATCH_IF\(([A-Z][A-Z]),([^,]*),([^\)]*)\)").unwrap();

        let mut bind_rules = String::new();
        let mut iter = op_regex.find_iter(&insts);
        while let Some(inst) = iter.next() {
            if abort_regex.is_match(inst.as_str()) {
                bind_rules.push_str("abort;")
            } else if let Some(caps) = abort_if_regex.captures(inst.as_str()) {
                let condition = Condition::try_from(caps.get(1).unwrap().as_str());
                let lhs = rename_and_add(&mut self.libraries, caps.get(2).unwrap().as_str().trim());
                let rhs = rename_and_add(&mut self.libraries, caps.get(3).unwrap().as_str().trim());
                let rule = match condition {
                    Ok(Condition::Equals) => Ok(format!("  {} != {};\n", lhs, rhs)),
                    Ok(Condition::NotEquals) => Ok(format!("  {} == {};\n", lhs, rhs)),
                    _ => Err("Unsupported condition"),
                }?;
                bind_rules.push_str(rule.as_str());
            } else if let Some(caps) = match_if_regex.captures(inst.as_str()) {
                let condition = Condition::try_from(caps.get(1).unwrap().as_str());
                let first_lhs = caps.get(2).unwrap().as_str().trim();
                let rhs = rename_and_add(&mut self.libraries, caps.get(3).unwrap().as_str().trim());

                bind_rules.push_str(
                    format!("  accept {} {{\n", rename_and_add(&mut self.libraries, first_lhs))
                        .as_str(),
                );
                bind_rules.push_str(format!("    {},\n", rhs).as_str());

                // We only support BI_MATCH_IF when we can convert it to an accept. So every remaining
                // op must be BI_MATCH_IF with the same LHS. We also only support EQ as the condition.
                assert_eq!(condition, Ok(Condition::Equals));
                while let Some(inst) = iter.next() {
                    if let Some(caps) = match_if_regex.captures(inst.as_str()) {
                        let condition = Condition::try_from(caps.get(1).unwrap().as_str());
                        assert_eq!(condition, Ok(Condition::Equals));

                        let lhs = caps.get(2).unwrap().as_str().trim();
                        let rhs = rename_and_add(
                            &mut self.libraries,
                            caps.get(3).unwrap().as_str().trim(),
                        );
                        assert_eq!(lhs, first_lhs);
                        bind_rules.push_str(format!("    {},\n", rhs).as_str());
                    } else {
                        return Err("Unsupported bind rules");
                    }
                }
                bind_rules.push_str("  }");
            } else {
                println!("The migration tool doesn't handle this bind op: {}", inst.as_str());
                return Err("Unhandled bind op");
            }
        }

        let name_regex = Regex::new(r"([A-Za-z0-9_])*\[\]").unwrap();
        let mut match_inst_name = name_regex
            .captures(insts)
            .ok_or("unable to get zx_bind_inst[] name")?
            .get(0)
            .unwrap()
            .as_str()
            .to_string();

        // Shed off the array brackets.
        match_inst_name.truncate(match_inst_name.len() - 2);
        self.match_insts.insert(match_inst_name, bind_rules);

        Ok(())
    }
}

pub fn process_source_file(input: PathBuf) -> Result<CompositeDeviceData, &'static str> {
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(input)
        .map_err(|_| "Failed to open build file")?;
    let mut contents = String::new();
    file.read_to_string(&mut contents).map_err(|_| "Failed to read source file")?;

    let bind_inst_regex = Regex::new(r"zx_bind_inst_t ([A-Za-z0-9_])*\[\] = \{([^}]*)\}").unwrap();
    if !bind_inst_regex.is_match(&contents) {
        return Err("No zx_bind_inst_t[] definition in source file");
    }

    // Add and migrate each bind_inst[] definition.
    let mut composite_bind = CompositeBindRules::new();
    let mut iter = bind_inst_regex.find_iter(&contents);
    while let Some(inst) = iter.next() {
        composite_bind.add_match_insts(inst.as_str())?;
    }

    // Get each device_fragment_part_t and match it to a migrated bind_inst[].
    let mut fragment_parts: HashMap<String, String> = HashMap::new();
    let part_regex = Regex::new(
        r"device_fragment_part_t (?P<part_var>[A-Za-z0-9_]*)\[\] = \{\s*\{[^,]*, (?P<match_var>[A-Za-z0-9_]*)}",
    )
    .unwrap();

    let mut iter = part_regex.captures_iter(&contents);
    while let Some(part_cap) = iter.next() {
        let part_var = capture_name(&part_cap, "part_var")?;
        let match_var = capture_name(&part_cap, "match_var")?;

        // Look up the zx_bind_inst_t[] with the matching variable name.
        let instructions = composite_bind
            .match_insts
            .get(&match_var)
            .ok_or("Undefined zx_bind_inst_t[] value in device_fragment_part_t")?;
        fragment_parts.insert(part_var, instructions.to_string());
    }

    // Get device_fragment_t.
    let fragment_list_regex = Regex::new(
        r#"device_fragment_t ([A-Za-z0-9_]*)\[\] = \{\s*(\{"[^"]*", [^,]*, [A-Za-z0-9_]*\},*\s*)*\s*}"#,
    ).unwrap();
    let fragment_regex =
        Regex::new(r#"\{"(?P<node>[^"]*)", [^,]*, (?P<part_var>[A-Za-z0-9_]*)\}"#).unwrap();

    let mut node_list: Vec<NodeData> = vec![];
    let mut fragment_list_iter = fragment_list_regex.find_iter(&contents);

    // TODO(spqchan): Support more than one fragment list.
    let mut cap_iter = fragment_regex.captures_iter(
        fragment_list_iter.next().ok_or("Unable to parse device_fragment_t")?.as_str(),
    );
    while let Some(fragment_cap) = cap_iter.next() {
        let node_name = capture_name(&fragment_cap, "node")?;
        let part_var = capture_name(&fragment_cap, "part_var")?;

        // Push the instructions from the matching device_fragment_part_t.
        let instructions = fragment_parts
            .get(&part_var.to_string())
            .ok_or("Undefined device_fragment_part_t value in device_fragment_t[]")?;
        node_list
            .push(NodeData { name: node_name.to_string(), bind_rules: instructions.to_string() });
    }

    if fragment_list_iter.next().is_some() {
        return Err("The migration tool only support one fragment list in the code.");
    }

    // If a pbus device description is available, then we need to add a pdev node.
    let device_desc = get_device_desc(&contents)?;
    if let Some(pdev) = device_desc.pbus_desc.as_ref() {
        let mut pdev_node_inst = String::new();
        pdev_node_inst
            .write_fmt(format_args!(
                include_str!("templates/pdev_bind.template"),
                pid = rename_and_add(&mut composite_bind.libraries, &pdev.pid),
                vid = rename_and_add(&mut composite_bind.libraries, &pdev.vid),
                did = rename_and_add(&mut composite_bind.libraries, &pdev.did),
                instance_id = pdev.instance_id,
            ))
            .map_err(|_| "Failed to format output")?;
        node_list.insert(0, NodeData { name: "pdev".to_string(), bind_rules: pdev_node_inst });
        composite_bind.libraries.insert(Library::Platform);
    }

    Ok(CompositeDeviceData {
        libraries: composite_bind.libraries,
        nodes: node_list,
        desc: device_desc,
    })
}
