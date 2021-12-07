// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {std::cell::RefCell, std::rc::Rc, tokio};

// A doubly-linked list of Notify objects. Each node represents a slot in the
// sequence. A task acquires its spot in the sequence by waiting on `ready`,
// then releases its spot by notifying the next task's `ready`. When a node is
// dropped, it removes itself from the list.
//
// This creates a reference-counting cycle. We break the cycle by removing a
// Node from the list when it is dropped (via Ticket or Lock).
//
// Our linked list needs the following features:
//
// - push back
// - an iterator with "next" and "erase" operations
// - the ability to hold multiple such iterators simultaneously
//
// The second feature is not provided by std::collections::LinkedList. It can be
// simulated with `split_at` at the cost of great awkwardness. The experimental
// cursors proposal (https://github.com/rust-lang/rust/issues/58533) supports the
// second feature via CursorMut, but CursorMuts require a mutable borrow on the
// list, which implies that only one async task can hold CursorMuts at any time.
// Hence we roll our own list.
type NodeRef = Rc<RefCell<Node>>;
type ListRef = Rc<RefCell<List>>;

struct Node {
    prev: Option<NodeRef>,
    next: Option<NodeRef>,
    list: ListRef,
    ready: Rc<tokio::sync::Notify>,
}

struct List {
    head: Option<NodeRef>,
    tail: Option<NodeRef>,
}

impl Node {
    fn new_at_tail(list: ListRef) -> Self {
        let old_tail = list.borrow().tail.clone();
        Node { prev: old_tail, next: None, list: list, ready: Rc::new(tokio::sync::Notify::new()) }
    }

    fn erase(&self) {
        // Disconnect from the linked list.
        if let Some(prev) = &self.prev {
            prev.borrow_mut().next = self.next.clone();
        }
        if let Some(next) = &self.next {
            next.borrow_mut().prev = self.prev.clone();
        }
        if let None = self.prev {
            self.list.borrow_mut().head = self.next.clone();
        }
        if let None = self.next {
            self.list.borrow_mut().tail = self.prev.clone();
        }

        // If this node was at the head of the list, the next node is ready.
        match (&self.prev, &self.next) {
            (None, Some(next)) => next.borrow().ready.notify(),
            _ => (),
        };
    }
}

pub struct Sequencer {
    list: ListRef,
}

/// A Sequencer can be used create a sequence of async tasks. To enter a sequence,
/// a task first obtains a `Ticket` which marks a slot in the sequence. Once the
/// task is ready to serialize with the sequence, it should await `Task::wait_turn()`,
/// then hold onto the returned `Lock` until it is ready to release the next ticket
/// in the sequence.
///
/// If a task drops its `Ticket` before awaiting `Task::wait_turn()`, the task's
/// slot is dropped from the sequence.
impl Sequencer {
    pub fn new() -> Sequencer {
        Sequencer { list: Rc::new(RefCell::new(List { head: None, tail: None })) }
    }

    pub fn next(&mut self) -> Ticket {
        let node = Rc::new(RefCell::new(Node::new_at_tail(self.list.clone())));

        // Node goes at the end of the list.
        let mut list = self.list.borrow_mut();
        if let Some(tail) = &list.tail {
            tail.borrow_mut().next = Some(node.clone());
        }
        list.tail = Some(node.clone());

        // The first node gets notified immediately.
        if list.head.is_none() {
            list.head = Some(node.clone());
            node.borrow().ready.notify();
        }

        Ticket { node: Some(node) }
    }
}

pub struct Ticket {
    node: Option<NodeRef>,
}

impl Ticket {
    pub async fn wait_turn(mut self) -> Lock {
        // Hold onto the node so it's not erased when self is dropped.
        let node = self.node.take().unwrap();
        // Clone the reference before awaiting so we don't hold `node.borrow()`
        // through the await. We need to allow concurrent mutations of `node` so
        // that `node.prev` can disconnect after it completes.
        let ready = node.borrow().ready.clone();
        ready.notified().await;
        Lock { node }
    }
}

impl Drop for Ticket {
    fn drop(&mut self) {
        match &self.node {
            Some(node) => node.borrow().erase(),
            None => (),
        }
    }
}

pub struct Lock {
    node: NodeRef,
}

impl Drop for Lock {
    fn drop(&mut self) {
        self.node.borrow().erase();
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, pretty_assertions::assert_eq, std::vec::Vec};

    fn new_result_vec() -> Rc<RefCell<Vec<u32>>> {
        Rc::new(RefCell::new(Vec::new()))
    }

    async fn wait_for_ticket(id: u32, t: Ticket, order: Rc<RefCell<Vec<u32>>>) {
        let _lock = t.wait_turn().await;
        order.borrow_mut().push(id);
    }

    #[fasync::run_until_stalled(test)]
    async fn in_order() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();

        let order = new_result_vec();
        futures::join!(
            wait_for_ticket(0, t0, order.clone()),
            wait_for_ticket(1, t1, order.clone()),
            wait_for_ticket(2, t2, order.clone()),
        );

        assert_eq!(*order.borrow(), vec![0, 1, 2]);
    }

    #[fasync::run_until_stalled(test)]
    async fn in_order_backwards_join() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();

        // Equivalent to in_order(), but pass the futures to join! in reverse order.
        let order = new_result_vec();
        futures::join!(
            wait_for_ticket(2, t2, order.clone()),
            wait_for_ticket(1, t1, order.clone()),
            wait_for_ticket(0, t0, order.clone()),
        );

        assert_eq!(*order.borrow(), vec![0, 1, 2]);
    }

    #[fasync::run_until_stalled(test)]
    async fn t0_dropped() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();

        let order = new_result_vec();
        std::mem::drop(t0);
        futures::join!(
            wait_for_ticket(1, t1, order.clone()),
            wait_for_ticket(2, t2, order.clone()),
        );

        assert_eq!(*order.borrow(), vec![1, 2]);
    }

    #[fasync::run_until_stalled(test)]
    async fn t0_t1_dropped() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();

        let order = new_result_vec();
        std::mem::drop(t0);
        std::mem::drop(t1);
        futures::join!(wait_for_ticket(2, t2, order.clone()),);

        assert_eq!(*order.borrow(), vec![2]);
    }

    #[fasync::run_until_stalled(test)]
    async fn t0_t2_dropped() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();

        let order = new_result_vec();
        std::mem::drop(t0);
        std::mem::drop(t2);
        futures::join!(wait_for_ticket(1, t1, order.clone()),);

        assert_eq!(*order.borrow(), vec![1]);
    }

    #[fasync::run_until_stalled(test)]
    async fn t1_dropped() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();

        let order = new_result_vec();
        std::mem::drop(t1);
        futures::join!(
            wait_for_ticket(0, t0, order.clone()),
            wait_for_ticket(2, t2, order.clone()),
        );

        assert_eq!(*order.borrow(), vec![0, 2]);
    }

    #[fasync::run_until_stalled(test)]
    async fn t1_t2_dropped() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();
        let t3 = s.next();

        let order = new_result_vec();
        std::mem::drop(t1);
        std::mem::drop(t2);
        futures::join!(wait_for_ticket(0, t0, order.clone()),);
        futures::join!(wait_for_ticket(3, t3, order.clone()),);

        assert_eq!(*order.borrow(), vec![0, 3]);
    }

    #[fasync::run_until_stalled(test)]
    async fn t2_dropped() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();

        let order = new_result_vec();
        std::mem::drop(t2);
        futures::join!(
            wait_for_ticket(0, t0, order.clone()),
            wait_for_ticket(1, t1, order.clone()),
        );

        assert_eq!(*order.borrow(), vec![0, 1]);
    }

    #[fasync::run_until_stalled(test)]
    async fn t1_t2_dropped_while_t0_locked() {
        let mut s = Sequencer::new();
        let t0 = s.next();
        let t1 = s.next();
        let t2 = s.next();
        let t3 = s.next();

        let order = new_result_vec();
        futures::join!(
            // Wait for t3 normally.
            wait_for_ticket(3, t3, order.clone()),
            // Drop t1 and t2 while holding t0's lock
            {
                let order = order.clone();
                async move {
                    let _lock = t0.wait_turn().await;
                    std::mem::drop(t1);
                    std::mem::drop(t2);
                    order.borrow_mut().push(0);
                }
            },
        );

        assert_eq!(*order.borrow(), vec![0, 3]);
    }
}
