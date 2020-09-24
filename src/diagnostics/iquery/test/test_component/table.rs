// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect as inspect,
    std::sync::atomic::{AtomicUsize, Ordering},
};

static CURRENT_SUFFIX: AtomicUsize = AtomicUsize::new(0);

pub fn reset_unique_names() {
    CURRENT_SUFFIX.store(0, Ordering::SeqCst);
}

pub fn unique_name(name: &str) -> String {
    let suffix = CURRENT_SUFFIX.fetch_add(1, Ordering::SeqCst);
    let result = format!("{}:0x{:x}", name, suffix);
    result
}

#[derive(Clone)]
pub enum NodeObject {
    Root(TableData),
    Table(TableData),
    Row(RowData),
    Cell(CellData),
}

#[derive(Clone)]
pub struct TableData {
    pub node_name: String,
    pub object_name: String,
    pub binary_data: Vec<u8>,
    pub rows: Vec<RowData>,
}

#[derive(Clone)]
pub struct RowData {
    pub node_name: String,
    pub cells: Vec<CellData>,
}

#[derive(Clone)]
pub struct CellData {
    pub node_name: String,
    pub name: String,
    pub value: i64,
    pub double_value: f64,
}

impl NodeObject {
    pub fn get_node_name(&self) -> String {
        match self {
            NodeObject::Root(_) => "root".to_string(),
            NodeObject::Table(t) => t.node_name.clone(),
            NodeObject::Row(r) => r.node_name.clone(),
            NodeObject::Cell(c) => c.node_name.clone(),
        }
    }
}

pub struct Table {
    rows: Vec<Row>,

    // For the VMO
    _node: inspect::Node,
    _object_name: inspect::StringProperty,
    _binary_data: inspect::BytesProperty,

    // TODO(fxbug.dev/41953): remove when the FIDL service is deprecated
    data: TableData,
}

impl Table {
    pub fn new(row_count: usize, col_count: usize, node_name: &str, node: inspect::Node) -> Self {
        let data = TableData {
            object_name: "Example Table".to_string(),
            binary_data: vec![0x20, 0x0, 0x11, 0x12, 0x5],
            node_name: node_name.to_string(),
            rows: vec![],
        };
        let _object_name = node.create_string("object_name", &data.object_name);
        let _binary_data = node.create_bytes("binary_data", data.binary_data.clone());
        let total = (row_count * col_count) as f64;
        let mut idx: f64 = 0.0;
        let mut rows = vec![];
        for i in 0..row_count {
            let node_name = unique_name("row");
            let mut row = Row::new(&node_name, node.create_child(&node_name));
            for j in 0..col_count {
                idx += 1.0;
                row.add_cell(&format!("({},{})", i, j), (i * j) as i64, 100.0 * idx / total);
            }
            rows.push(row);
        }
        Self { _node: node, _object_name, _binary_data, rows, data }
    }

    pub fn get_node_object(&self) -> NodeObject {
        let mut data = self.data.clone();
        data.rows = self.rows.iter().map(|row| row.get_data()).collect();
        NodeObject::Root(data)
    }
}

struct Cell {
    _node: inspect::Node,
    _name: inspect::StringProperty,
    _value: inspect::IntProperty,
    _double_value: inspect::DoubleProperty,

    // TODO(fxbug.dev/41953): remove when the FIDL service is deprecated.
    data: CellData,
}

impl Cell {
    fn new(
        name: &str,
        value: i64,
        double_value: f64,
        node_name: &str,
        node: inspect::Node,
    ) -> Self {
        let _name = node.create_string("name", name);
        let _value = node.create_int("value", value);
        let _double_value = node.create_double("double_value", double_value);
        let data = CellData {
            node_name: node_name.to_string(),
            name: name.to_string(),
            value,
            double_value,
        };
        Self { data, _node: node, _name, _value, _double_value }
    }
}

struct Row {
    node: inspect::Node,
    cells: Vec<Cell>,

    // TODO(fxbug.dev/41953): remove when the FIDL service is deprecated.
    data: RowData,
}

impl Row {
    fn new(node_name: &str, node: inspect::Node) -> Self {
        let data = RowData { node_name: node_name.to_string(), cells: vec![] };
        Self { data, node, cells: vec![] }
    }

    fn add_cell(&mut self, name: &str, value: i64, double_value: f64) {
        let node_name = unique_name("cell");
        self.cells.push(Cell::new(
            name,
            value,
            double_value,
            &node_name,
            self.node.create_child(&node_name),
        ));
    }

    fn get_data(&self) -> RowData {
        let mut data = self.data.clone();
        data.cells = self.cells.iter().map(|cell| cell.data.clone()).collect();
        data
    }
}
