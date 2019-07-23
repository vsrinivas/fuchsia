use NodeId;

///
/// A `Node` builder that provides more control over how a `Node` is created.
///
pub struct NodeBuilder<T> {
    data: T,
    child_capacity: usize,
}

impl<T> NodeBuilder<T> {
    ///
    /// Creates a new `NodeBuilder` with the required data.
    ///
    /// ```
    /// use id_tree::NodeBuilder;
    ///
    /// let _node_builder = NodeBuilder::new(5);
    /// ```
    ///
    pub fn new(data: T) -> NodeBuilder<T> {
        NodeBuilder {
            data: data,
            child_capacity: 0,
        }
    }

    ///
    /// Set the child capacity of the `NodeBuilder`.
    ///
    /// As `Node`s are added to a `Tree`, parent and child references must be maintained. To do
    /// this, an allocation must be made every time a child is added to a `Node`.  Using this
    /// setting allows the `Node` to pre-allocate space for its children so that the allocations
    /// aren't made as children are added.
    ///
    /// _Use of this setting is recommended if you know the **maximum number** of children (not
    /// including grandchildren, great-grandchildren, etc.) that a `Node` will have **at any given
    /// time**_.
    ///
    /// ```
    /// use id_tree::NodeBuilder;
    ///
    /// let _node_builder = NodeBuilder::new(5).with_child_capacity(3);
    /// ```
    ///
    pub fn with_child_capacity(mut self, child_capacity: usize) -> NodeBuilder<T> {
        self.child_capacity = child_capacity;
        self
    }

    ///
    /// Build a `Node` based upon the current settings in the `NodeBuilder`.
    ///
    /// ```
    /// use id_tree::NodeBuilder;
    /// use id_tree::Node;
    ///
    /// let node: Node<i32> = NodeBuilder::new(5)
    ///         .with_child_capacity(3)
    ///         .build();
    ///
    /// assert_eq!(node.data(), &5);
    /// assert_eq!(node.children().capacity(), 3);
    /// ```
    ///
    pub fn build(self) -> Node<T> {
        Node {
            data: self.data,
            parent: None,
            children: Vec::with_capacity(self.child_capacity),
        }
    }
}

///
/// A container that wraps data in a given `Tree`.
///
#[derive(Debug)]
pub struct Node<T> {
    pub(crate) data: T,
    pub(crate) parent: Option<NodeId>,
    pub(crate) children: Vec<NodeId>,
}

impl<T> PartialEq for Node<T>
where
    T: PartialEq,
{
    fn eq(&self, other: &Node<T>) -> bool {
        self.data == other.data
    }
}

impl<T> Node<T> {
    ///
    /// Creates a new `Node` with the data provided.
    ///
    /// ```
    /// use id_tree::Node;
    ///
    /// let _one: Node<i32> = Node::new(1);
    /// ```
    ///
    pub fn new(data: T) -> Node<T> {
        NodeBuilder::new(data).build()
    }

    ///
    /// Returns an immutable reference to the data contained within the `Node`.
    ///
    /// ```
    /// use id_tree::Node;
    ///
    /// let node_three: Node<i32> = Node::new(3);
    /// let three = 3;
    ///
    /// assert_eq!(node_three.data(), &three);
    /// ```
    ///
    pub fn data(&self) -> &T {
        &self.data
    }

    ///
    /// Returns a mutable reference to the data contained within the `Node`.
    ///
    /// ```
    /// use id_tree::Node;
    ///
    /// let mut node_four: Node<i32> = Node::new(4);
    /// let mut four = 4;
    ///
    /// assert_eq!(node_four.data_mut(), &mut four);
    /// ```
    ///
    pub fn data_mut(&mut self) -> &mut T {
        &mut self.data
    }

    ///
    /// Replaces this `Node`s data with the data provided.
    ///
    /// Returns the old value of data.
    ///
    /// ```
    /// use id_tree::Node;
    ///
    /// let mut node_four: Node<i32> = Node::new(3);
    ///
    /// // ops! lets correct this
    /// let three = node_four.replace_data(4);
    ///
    /// assert_eq!(node_four.data(), &4);
    /// assert_eq!(three, 3);
    /// ```
    ///
    pub fn replace_data(&mut self, mut data: T) -> T {
        ::std::mem::swap(&mut data, self.data_mut());
        data
    }

    ///
    /// Returns a `Some` value containing the `NodeId` of this `Node`'s parent if it exists; returns
    /// `None` if it does not.
    ///
    /// **Note:** A `Node` cannot have a parent until after it has been inserted into a `Tree`.
    ///
    /// ```
    /// use id_tree::Node;
    ///
    /// let five: Node<i32> = Node::new(5);
    ///
    /// assert!(five.parent().is_none());
    /// ```
    ///
    pub fn parent(&self) -> Option<&NodeId> {
        self.parent.as_ref()
    }

    ///
    /// Returns an immutable reference to a `Vec` containing the `NodeId`s of this `Node`'s
    /// children.
    ///
    /// **Note:** A `Node` cannot have any children until after it has been inserted into a `Tree`.
    ///
    /// ```
    /// use id_tree::Node;
    ///
    /// let six: Node<i32> = Node::new(6);
    ///
    /// assert_eq!(six.children().len(), 0);
    /// ```
    ///
    pub fn children(&self) -> &Vec<NodeId> {
        &self.children
    }

    pub(crate) fn set_parent(&mut self, parent: Option<NodeId>) {
        self.parent = parent;
    }

    pub(crate) fn add_child(&mut self, child: NodeId) {
        self.children.push(child);
    }

    pub(crate) fn replace_child(&mut self, old: NodeId, new: NodeId) {
        let index = self
            .children()
            .iter()
            .enumerate()
            .find(|&(_, id)| id == &old)
            .unwrap()
            .0;

        let children = self.children_mut();
        children.push(new);
        children.swap_remove(index);
    }

    pub(crate) fn children_mut(&mut self) -> &mut Vec<NodeId> {
        &mut self.children
    }

    pub(crate) fn set_children(&mut self, children: Vec<NodeId>) {
        self.children = children;
    }

    pub(crate) fn take_children(&mut self) -> Vec<NodeId> {
        use std::mem;

        let mut empty = Vec::with_capacity(0);
        mem::swap(&mut self.children, &mut empty);
        empty //not so empty anymore
    }
}

#[cfg(test)]
mod node_builder_tests {
    use super::NodeBuilder;

    #[test]
    fn test_new() {
        let five = 5;
        let node = NodeBuilder::new(5).build();
        assert_eq!(node.data(), &five);
        assert_eq!(node.children.capacity(), 0);
    }

    #[test]
    fn test_with_child_capacity() {
        let five = 5;
        let node = NodeBuilder::new(5).with_child_capacity(10).build();
        assert_eq!(node.data(), &five);
        assert_eq!(node.children.capacity(), 10);
    }
}

#[cfg(test)]
mod node_tests {
    use super::super::snowflake::ProcessUniqueId;
    use super::super::NodeId;
    use super::Node;

    #[test]
    fn test_new() {
        let node = Node::new(5);
        assert_eq!(node.children.capacity(), 0);
    }

    #[test]
    fn test_data() {
        let five = 5;
        let node = Node::new(five);
        assert_eq!(node.data(), &five);
    }

    #[test]
    fn test_data_mut() {
        let mut five = 5;
        let mut node = Node::new(five);
        assert_eq!(node.data_mut(), &mut five);
    }

    #[test]
    fn test_parent() {
        let mut node = Node::new(5);
        assert!(node.parent().is_none());

        let parent_id: NodeId = NodeId {
            tree_id: ProcessUniqueId::new(),
            index: 0,
        };

        node.set_parent(Some(parent_id.clone()));
        assert!(node.parent().is_some());

        assert_eq!(node.parent().unwrap().clone(), parent_id);
    }

    #[test]
    fn test_children() {
        let mut node = Node::new(5);
        assert_eq!(node.children().len(), 0);

        let child_id: NodeId = NodeId {
            tree_id: ProcessUniqueId::new(),
            index: 0,
        };
        node.add_child(child_id.clone());

        assert_eq!(node.children().len(), 1);
        assert_eq!(node.children().get(0).unwrap(), &child_id);

        let mut node = Node::new(5);
        assert_eq!(node.children().len(), 0);

        let child_id: NodeId = NodeId {
            tree_id: ProcessUniqueId::new(),
            index: 0,
        };
        node.children_mut().push(child_id.clone());

        assert_eq!(node.children().len(), 1);
        assert_eq!(node.children().get(0).unwrap(), &child_id);
    }

    #[test]
    fn test_partial_eq() {
        let node1 = Node::new(42);
        let node2 = Node::new(42);
        let node3 = Node::new(23);
        assert_eq!(node1, node2);
        assert_ne!(node1, node3);
        assert_ne!(node2, node3);
    }
}
