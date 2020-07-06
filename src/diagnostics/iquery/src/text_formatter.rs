// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::reader::{ArrayContent, NodeHierarchy, Property},
    nom::HexDisplay,
    num_traits::Bounded,
    std::{
        fmt,
        ops::{Add, AddAssign, MulAssign},
    },
};

const INDENT: usize = 2;
const HEX_DISPLAY_CHUNK_SIZE: usize = 16;

pub fn format(path: &str, node_hierarchy: NodeHierarchy) -> String {
    let result = output_hierarchy(node_hierarchy, 1);
    format!("{}:\n{}", path, result)
}

fn output_hierarchy(node_hierarchy: NodeHierarchy, indent: usize) -> String {
    let mut lines = vec![];
    let name_indent = " ".repeat(INDENT * indent);
    let value_indent = " ".repeat(INDENT * (indent + 1));

    lines.push(format!("{}{}:", name_indent, node_hierarchy.name));

    lines.extend(node_hierarchy.properties.into_iter().map(|property| match property {
        Property::String(name, value) => format!("{}{} = {}", value_indent, name, value),
        Property::Int(name, value) => format!("{}{} = {}", value_indent, name, value),
        Property::Uint(name, value) => format!("{}{} = {}", value_indent, name, value),
        Property::Double(name, value) => format!("{}{} = {:.6}", value_indent, name, value),
        Property::Bytes(name, array) => {
            let byte_str = array.to_hex(HEX_DISPLAY_CHUNK_SIZE);
            format!("{}{} = Binary:\n{}", value_indent, name, byte_str.trim())
        }
        Property::Bool(name, value) => format!("{}{} = {}", value_indent, name, value),
        Property::IntArray(name, array) => output_array(&value_indent, &name, &array),
        Property::UintArray(name, array) => output_array(&value_indent, &name, &array),
        Property::DoubleArray(name, array) => output_array(&value_indent, &name, &array),
    }));

    lines.extend(
        node_hierarchy.children.into_iter().map(|child| output_hierarchy(child, indent + 1)),
    );

    lines.join("\n")
}

fn output_array<
    T: AddAssign + MulAssign + Copy + Add<Output = T> + fmt::Display + NumberFormat + Bounded,
>(
    value_indent: &str,
    name: &str,
    array: &ArrayContent<T>,
) -> String {
    let content = match array {
        ArrayContent::Values(values) => {
            values.iter().map(|x| x.to_string()).collect::<Vec<String>>()
        }
        ArrayContent::Buckets(buckets) => buckets
            .iter()
            .map(|bucket| {
                format!(
                    "[{},{})={}",
                    bucket.floor.format(),
                    bucket.upper.format(),
                    bucket.count.format()
                )
            })
            .collect::<Vec<String>>(),
    };
    format!("{}{} = [{}]", value_indent, name, content.join(", "))
}

trait NumberFormat {
    fn format(&self) -> String;
}

impl NumberFormat for i64 {
    fn format(&self) -> String {
        match *self {
            std::i64::MAX => "<max>".to_string(),
            std::i64::MIN => "<min>".to_string(),
            x => format!("{}", x),
        }
    }
}

impl NumberFormat for u64 {
    fn format(&self) -> String {
        match *self {
            std::u64::MAX => "<max>".to_string(),
            x => format!("{}", x),
        }
    }
}

impl NumberFormat for f64 {
    fn format(&self) -> String {
        if *self == std::f64::MAX || *self == std::f64::INFINITY {
            "inf".to_string()
        } else if *self == std::f64::MIN || *self == std::f64::NEG_INFINITY {
            "-inf".to_string()
        } else {
            format!("{}", self)
        }
    }
}
