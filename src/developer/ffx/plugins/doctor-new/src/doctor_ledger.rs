// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ledger_view::*,
    anyhow::{anyhow, Result},
    std::io::Write,
};

const DEFAULT_OUTCOME_VALUE: LedgerOutcome = LedgerOutcome::Success;
const DEFAULT_OUTCOME_FUNCTION: OutcomeFoldFunction = OutcomeFoldFunction::SuccessToFailure;
const DEFAULT_LEDGER_MODE: LedgerMode = LedgerMode::Normal;

// Main interface for LedgerNode.
pub trait LedgerNodeOp {
    fn add(&mut self, node: LedgerNode) -> Result<usize>;
    fn close(&mut self, depth: usize) -> Result<()>;
    fn set_outcome(&mut self, depth: usize, outcome: LedgerOutcome) -> Result<()>;
    fn set_fold_function(&mut self, function: OutcomeFoldFunction, default: LedgerOutcome);
    fn make_view(&self, depth: usize, display_mode: LedgerViewMode) -> Option<LedgerViewNode>;
    fn make_all(&self, display_mode: LedgerViewMode) -> Vec<LedgerViewNode>;
}

pub struct LedgerNodeValue {
    data: String,
    outcome: LedgerOutcome,
    mode: LedgerMode,
}

pub struct LedgerNode {
    value: LedgerNodeValue,
    children: Vec<LedgerNode>,
    is_closed: bool,
    outcome_fold_function: OutcomeFoldFunction,
    outcome_default_value: LedgerOutcome,
}

// Constructor and internal helper methods for LedgerNode.
impl LedgerNode {
    pub fn new(data: String, mode: LedgerMode) -> Self {
        LedgerNode {
            value: LedgerNodeValue { data, outcome: LedgerOutcome::Automatic, mode },
            children: Vec::new(),
            is_closed: false,
            outcome_fold_function: DEFAULT_OUTCOME_FUNCTION,
            outcome_default_value: DEFAULT_OUTCOME_VALUE,
        }
    }

    // Return the outcome of the node. If the outcome is not set, infer it from its children's
    // output, the default value, and the fold function.
    fn get_node_outcome(&self) -> LedgerOutcome {
        if self.value.outcome != LedgerOutcome::Automatic {
            return self.value.outcome;
        }

        if self.children.len() == 0 {
            return self.outcome_default_value;
        }

        match self.outcome_fold_function {
            OutcomeFoldFunction::SuccessToFailure => {
                self.children.iter().fold(LedgerOutcome::ValidRangeStart, |acc, child| {
                    acc.max(child.get_node_outcome().valid_or(LedgerOutcome::ValidRangeStart))
                })
            }
            OutcomeFoldFunction::FailureToSuccess => {
                self.children.iter().fold(LedgerOutcome::ValidRangeEnd, |acc, child| {
                    acc.min(child.get_node_outcome().valid_or(LedgerOutcome::ValidRangeEnd))
                })
            }
        }
        .valid_or(self.outcome_default_value)
    }

    // Return the mode of the node. If the mode is not set, infer it from its children's mode.
    fn get_node_mode(&self) -> LedgerMode {
        if self.value.mode != LedgerMode::Automatic {
            return self.value.mode;
        }

        if self.children.len() == 0 {
            return DEFAULT_LEDGER_MODE;
        }

        return self
            .children
            .iter()
            .fold(LedgerMode::Verbose, |acc, child| acc.min(child.get_node_mode()));
    }

    // Determine based on settings if the node should be displayed.
    fn should_display_node(&self, display_mode: LedgerViewMode) -> bool {
        match display_mode {
            LedgerViewMode::Verbose => true,
            LedgerViewMode::Normal => match self.get_node_mode() {
                LedgerMode::Normal => true,
                _ => false,
            },
        }
    }

    // Recursive function to set the latest node's outcome at the specified end_depth.
    fn set_outcome_at_depth(
        &mut self,
        cur_depth: usize,
        end_depth: usize,
        outcome: LedgerOutcome,
    ) -> Result<()> {
        if cur_depth >= end_depth {
            self.value.outcome = outcome;
            return Ok(());
        } else if let Some(last_child) = self.children.last_mut() {
            return last_child.set_outcome_at_depth(cur_depth + 1, end_depth, outcome);
        } else {
            return Err(anyhow!("Cannot set outcome at depth {}, node does not exist", end_depth));
        }
    }

    // Add new node as a child of the most current open node, return depth.
    fn add_at_max_depth(&mut self, node: LedgerNode, depth: usize) -> Result<usize> {
        if self.is_closed {
            return Err(anyhow!("Add error: Ledger node at depth {} is closed", depth));
        }

        if let Some(last_child) = self.children.last_mut() {
            if last_child.is_closed == false {
                return last_child.add_at_max_depth(node, depth + 1);
            }
        }

        self.children.push(node);
        return Ok(depth + 1);
    }

    // Recursive function to close the latest node at the specified end_depth.
    fn close_at_depth(&mut self, cur_depth: usize, end_depth: usize) -> Result<()> {
        if cur_depth >= end_depth {
            self.is_closed = true;
            return Ok(());
        } else if let Some(last_child) = self.children.last_mut() {
            return last_child.close_at_depth(cur_depth + 1, end_depth);
        } else {
            return Err(anyhow!("Cannot close node at depth {}, node does not exist", end_depth));
        }
    }

    // Recursive function to calculate the latest node's outcome at the specified end_depth.
    fn calc_outcome_at_depth(&self, depth: usize) -> LedgerOutcome {
        if depth == 0 {
            return self.get_node_outcome();
        } else if let Some(last_child) = self.children.last() {
            return last_child.calc_outcome_at_depth(depth - 1);
        } else {
            return LedgerOutcome::Automatic;
        }
    }

    // Make a simplified (display) node tree.
    fn make_node_view(&self, display_mode: LedgerViewMode) -> Option<LedgerViewNode> {
        if !self.should_display_node(display_mode) {
            return None;
        }

        let outcome = LedgerViewOutcome::from(self.get_node_outcome());
        let mut output_node =
            LedgerViewNode { data: self.value.data.clone(), outcome, children: Vec::new() };

        for child in self.children.iter() {
            if let Some(node) = child.make_node_view(display_mode) {
                output_node.children.push(node);
            }
        }

        return Some(output_node);
    }
}

impl LedgerNodeOp for LedgerNode {
    // Add node as a child of the most recent open node, return depth
    fn add(&mut self, node: LedgerNode) -> Result<usize> {
        return self.add_at_max_depth(node, 0);
    }

    // Close the most recent node at the specified depth.
    fn close(&mut self, depth: usize) -> Result<()> {
        return self.close_at_depth(0, depth);
    }

    // Set the most recent node's outcome at the specified depth.
    fn set_outcome(&mut self, depth: usize, outcome: LedgerOutcome) -> Result<()> {
        return self.set_outcome_at_depth(0, depth, outcome);
    }

    // Set the node's fold function. If the node's outcome is not set, this fold function
    // determines how the outcome is inferred from its children nodes.
    fn set_fold_function(&mut self, function: OutcomeFoldFunction, default: LedgerOutcome) {
        self.outcome_fold_function = function;
        self.outcome_default_value = default;
    }

    // Return the display node tree for the most recent node at the specified depth.
    fn make_view(&self, depth: usize, display_mode: LedgerViewMode) -> Option<LedgerViewNode> {
        if depth == 0 {
            return self.make_node_view(display_mode);
        } else {
            return match self.children.last() {
                Some(child) => child.make_view(depth - 1, display_mode),
                None => None,
            };
        }
    }

    // Return all display nodes at depth 1.
    fn make_all(&self, display_mode: LedgerViewMode) -> Vec<LedgerViewNode> {
        let mut output = Vec::<LedgerViewNode>::new();
        for child in &self.children {
            if let Some(node) = child.make_node_view(display_mode) {
                output.push(node);
            }
        }
        output
    }
}

#[derive(Copy, Clone, PartialOrd, Ord, PartialEq, Eq)]
pub enum LedgerOutcome {
    ValidRangeStart,
    Success,
    Warning,
    Failure,
    ValidRangeEnd,
    Automatic,
    // Soft values are ignored when computing the outcome of parent.
    SoftWarning,
}

impl LedgerOutcome {
    pub fn valid_or(self, default: LedgerOutcome) -> Self {
        if self > LedgerOutcome::ValidRangeStart && self < LedgerOutcome::ValidRangeEnd {
            return self;
        } else {
            return default;
        }
    }
}

pub enum OutcomeFoldFunction {
    SuccessToFailure,
    FailureToSuccess,
}

// Mode type for nodes.
#[derive(Debug, Copy, Clone, PartialOrd, Ord, PartialEq, Eq)]
pub enum LedgerMode {
    Normal,
    Verbose,
    Automatic,
}

#[derive(Copy, Clone)]
pub enum LedgerViewMode {
    Normal,
    Verbose,
}

// DoctorLedger:
// Builds and keeps track of a LedgerNode tree to automatically decide when and what display data
// to send (to a member variable that implements LedgerView). The display data consists of a
// LedgerViewNode tree that is constructed from a LedgerNode tree and the ledger mode settings.
//
// Internal representation of the LedgerNode tree:
// * Root: No data.
// * First level nodes: Data that can be modified. Invoke display when this node is closed.
// * 2nd Level+: Data that can be modified.
//
pub struct DoctorLedger<W: Write> {
    pub writer: W,
    root_node: LedgerNode,
    ledger_view: Box<dyn LedgerView>,
    ledger_mode: LedgerViewMode,
}

impl<W: Write> DoctorLedger<W> {
    pub fn new(writer: W, view: Box<dyn LedgerView>, mode: LedgerViewMode) -> DoctorLedger<W> {
        DoctorLedger {
            writer,
            root_node: LedgerNode::new("".to_string(), LedgerMode::Normal),
            ledger_view: view,
            ledger_mode: mode,
        }
    }

    pub fn add(&mut self, node: LedgerNode) -> Result<usize> {
        return self.root_node.add(node);
    }

    pub fn add_node(&mut self, data: &str, mode: LedgerMode) -> Result<usize> {
        return self.add(LedgerNode::new(data.to_string(), mode));
    }

    pub fn calc_outcome(&self, depth: usize) -> LedgerOutcome {
        return self.root_node.calc_outcome_at_depth(depth);
    }

    pub fn set_outcome(&mut self, depth: usize, outcome: LedgerOutcome) -> Result<()> {
        self.root_node.set_outcome(depth, outcome)?;
        self.close(depth)?;
        return Ok(());
    }

    pub fn close(&mut self, depth: usize) -> Result<()> {
        if depth == 1 {
            self.display()?;
        }
        return self.root_node.close(depth);
    }

    pub fn get_ledger_mode(&self) -> LedgerViewMode {
        return self.ledger_mode;
    }

    fn display(&mut self) -> Result<()> {
        match self.root_node.make_view(1, self.ledger_mode) {
            Some(node) => {
                self.ledger_view.set(node);
                write!(self.writer, "{}", self.ledger_view)?;
            }
            None => (),
        }
        return Ok(());
    }

    pub fn write_all(&self, ledger_view: &mut dyn LedgerView) -> Result<String> {
        let mut output = "".to_string();
        for node in self.root_node.make_all(self.ledger_mode) {
            ledger_view.set(node);
            output = format!("{}{}", output, ledger_view);
        }
        return Ok(output);
    }
}

#[cfg(test)]
mod test {
    use {super::*, ffx_doctor_test_utils::MockWriter, std::fmt};

    const MODE_VERBOSE: LedgerMode = LedgerMode::Verbose;
    const MODE_NORMAL: LedgerMode = LedgerMode::Normal;
    const MODE_DEFAULT: LedgerMode = LedgerMode::Automatic;
    const INDENT_STR: &str = "    ";

    pub fn doctorledger_test_new(
        view: Box<dyn LedgerView>,
        mode: LedgerViewMode,
    ) -> DoctorLedger<MockWriter> {
        DoctorLedger::<MockWriter>::new(MockWriter::new(), view, mode)
    }

    pub fn doctorledger_debug(ledger: &DoctorLedger<MockWriter>) -> String {
        ledger.writer.get_data()
    }

    struct FakeLedgerView {
        tree: LedgerViewNode,
    }

    impl FakeLedgerView {
        pub fn new() -> Self {
            FakeLedgerView { tree: LedgerViewNode::default() }
        }
        fn gen_output(&self, parent_node: &LedgerViewNode, indent_level: usize) -> String {
            let mut output_str = format!(
                "{}[{}] {}\n",
                INDENT_STR.repeat(indent_level),
                parent_node.outcome.format(false),
                &parent_node.data
            );

            for child_node in &parent_node.children {
                let child_str = self.gen_output(child_node, indent_level + 1);
                output_str = format!("{}{}", output_str, child_str);
            }

            return output_str;
        }
    }

    impl fmt::Display for FakeLedgerView {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "{}", self.gen_output(&self.tree, 0))
        }
    }

    impl LedgerView for FakeLedgerView {
        fn set(&mut self, new_tree: LedgerViewNode) {
            self.tree = new_tree;
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_outcome() -> Result<()> {
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = doctorledger_test_new(ledger_view, LedgerViewMode::Verbose);
        let mut main_node: usize;

        main_node = ledger.add(LedgerNode::new("a".to_string(), MODE_VERBOSE))?;
        ledger.set_outcome(main_node, LedgerOutcome::Success)?;
        main_node = ledger.add(LedgerNode::new("b".to_string(), MODE_VERBOSE))?;
        ledger.set_outcome(main_node, LedgerOutcome::Warning)?;
        main_node = ledger.add(LedgerNode::new("c".to_string(), MODE_VERBOSE))?;
        ledger.set_outcome(main_node, LedgerOutcome::Failure)?;

        assert_eq!(
            doctorledger_debug(&ledger),
            "\
                \n[✓] a\
                \n[!] b\
                \n[✗] c\n"
        );
        Ok(())
    }

    fn setup_simple_mode(ledger: &mut DoctorLedger<MockWriter>) -> Result<()> {
        let mut main_node: usize;

        main_node = ledger.add(LedgerNode::new("a".to_string(), MODE_VERBOSE))?;
        ledger.set_outcome(main_node, LedgerOutcome::Success)?;
        main_node = ledger.add(LedgerNode::new("b".to_string(), MODE_NORMAL))?;
        ledger.set_outcome(main_node, LedgerOutcome::Warning)?;
        main_node = ledger.add(LedgerNode::new("c".to_string(), MODE_DEFAULT))?;
        ledger.set_outcome(main_node, LedgerOutcome::Failure)?;
        main_node = ledger.add(LedgerNode::new("d".to_string(), MODE_VERBOSE))?;
        ledger.set_outcome(main_node, LedgerOutcome::Failure)?;
        main_node = ledger.add(LedgerNode::new("e".to_string(), MODE_NORMAL))?;
        ledger.set_outcome(main_node, LedgerOutcome::Success)?;
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mode_verbose() -> Result<()> {
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = doctorledger_test_new(ledger_view, LedgerViewMode::Verbose);
        setup_simple_mode(&mut ledger)?;

        assert_eq!(
            doctorledger_debug(&ledger),
            "\
                \n[✓] a\
                \n[!] b\
                \n[✗] c\
                \n[✗] d\
                \n[✓] e\
                \n"
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mode_normal() -> Result<()> {
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = doctorledger_test_new(ledger_view, LedgerViewMode::Normal);
        setup_simple_mode(&mut ledger)?;

        assert_eq!(
            doctorledger_debug(&ledger),
            "\
                \n[!] b\
                \n[✗] c\
                \n[✓] e\
                \n"
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_group_outcome() -> Result<()> {
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = doctorledger_test_new(ledger_view, LedgerViewMode::Normal);
        let mut main_node: usize;

        main_node = ledger.add(LedgerNode::new("a".to_string(), MODE_NORMAL))?;
        {
            let node_1 = ledger.add(LedgerNode::new("1".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_1, LedgerOutcome::Success)?;
            let node_2 = ledger.add(LedgerNode::new("2".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_2, LedgerOutcome::Success)?;
            let node_3 = ledger.add(LedgerNode::new("3".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_3, LedgerOutcome::Success)?;
        }
        ledger.close(main_node)?;
        main_node = ledger.add(LedgerNode::new("b".to_string(), MODE_NORMAL))?;
        {
            let node_1 = ledger.add(LedgerNode::new("1".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_1, LedgerOutcome::Success)?;
            let node_2 = ledger.add(LedgerNode::new("2".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_2, LedgerOutcome::Warning)?;
            let node_3 = ledger.add(LedgerNode::new("3".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_3, LedgerOutcome::Success)?;
        }
        ledger.close(main_node)?;
        main_node = ledger.add(LedgerNode::new("c".to_string(), MODE_NORMAL))?;
        {
            let node_1 = ledger.add(LedgerNode::new("1".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_1, LedgerOutcome::Success)?;
            let node_2 = ledger.add(LedgerNode::new("2".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_2, LedgerOutcome::SoftWarning)?;
            let node_3 = ledger.add(LedgerNode::new("3".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_3, LedgerOutcome::Success)?;
        }
        ledger.close(main_node)?;
        main_node = ledger.add(LedgerNode::new("d".to_string(), MODE_NORMAL))?;
        {
            let node_1 = ledger.add(LedgerNode::new("1".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_1, LedgerOutcome::Success)?;
            let node_2 = ledger.add(LedgerNode::new("2".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_2, LedgerOutcome::Failure)?;
            let node_3 = ledger.add(LedgerNode::new("3".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_3, LedgerOutcome::Success)?;
        }
        ledger.close(main_node)?;

        assert_eq!(
            doctorledger_debug(&ledger),
            "\
                \n[✓] a\
                \n[!] b\
                \n[✓] c\
                \n[✗] d\
                \n"
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_group_outcome_custom() -> Result<()> {
        let ledger_view = Box::new(FakeLedgerView::new());
        let mut ledger = doctorledger_test_new(ledger_view, LedgerViewMode::Normal);
        let mut main_node: usize;

        let mut node = LedgerNode::new("a".to_string(), MODE_NORMAL);
        node.set_fold_function(OutcomeFoldFunction::FailureToSuccess, LedgerOutcome::Failure);
        main_node = ledger.add(node)?;
        ledger.close(main_node)?;

        node = LedgerNode::new("b".to_string(), MODE_NORMAL);
        node.set_fold_function(OutcomeFoldFunction::FailureToSuccess, LedgerOutcome::Failure);
        main_node = ledger.add(node)?;
        {
            let node_1 = ledger.add(LedgerNode::new("1".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_1, LedgerOutcome::Failure)?;
            let node_2 = ledger.add(LedgerNode::new("2".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_2, LedgerOutcome::SoftWarning)?;
        }
        ledger.close(main_node)?;

        node = LedgerNode::new("c".to_string(), MODE_NORMAL);
        node.set_fold_function(OutcomeFoldFunction::FailureToSuccess, LedgerOutcome::Failure);
        main_node = ledger.add(node)?;
        {
            let node_1 = ledger.add(LedgerNode::new("1".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_1, LedgerOutcome::Failure)?;
            let node_2 = ledger.add(LedgerNode::new("2".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_2, LedgerOutcome::Success)?;
            let node_3 = ledger.add(LedgerNode::new("3".to_string(), MODE_VERBOSE))?;
            ledger.set_outcome(node_3, LedgerOutcome::Failure)?;
        }
        ledger.close(main_node)?;

        assert_eq!(
            doctorledger_debug(&ledger),
            "\
                \n[✗] a\
                \n[✗] b\
                \n[✓] c\
                \n"
        );
        Ok(())
    }
}
