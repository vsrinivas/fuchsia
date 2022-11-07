use std::fmt::{self, Display};

/// a simple recursive type which is able to render its
/// components in a tree-like format
#[derive(Debug)]
pub struct Tree<D: Display>(D, Vec<Tree<D>>);

impl<D: Display> Tree<D> {
    pub fn new(root: D, leaves: Vec<Tree<D>>) -> Tree<D> {
        Tree(root, leaves)
    }

    pub fn root(root: D) -> Tree<D> {
        Tree(root, Vec::new())
    }

    pub fn push(&mut self, leaf: Tree<D>) -> &mut Self {
        self.1.push(leaf);
        self
    }

    fn display_leaves(f: &mut fmt::Formatter,
                      leaves: &Vec<Tree<D>>,
                      spaces: Vec<bool>)
                      -> fmt::Result {
        for (i, leaf) in leaves.iter().enumerate() {
            let last = i >= leaves.len() - 1;
            let mut clone = spaces.clone();
            // print single line
            for s in &spaces {
                if *s {
                    let _ = write!(f, "    ");
                } else {
                    let _ = write!(f, "|   ");
                }
            }
            if last {
                let _ = writeln!(f, "└── {}", leaf.0);
            } else {
                let _ = writeln!(f, "├── {}", leaf.0);
            }

            // recurse
            if !leaf.1.is_empty() {
                clone.push(last);
                let _ = Self::display_leaves(f, &leaf.1, clone);
            }
        }
        write!(f, "")
    }
}

impl<D: Display> Display for Tree<D> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let _ = writeln!(f, "{}", self.0);
        Self::display_leaves(f, &self.1, Vec::new())
    }
}

#[cfg(test)]
mod tests {
    use super::Tree;
    #[test]
    fn render_tree_root() {
        let tree = Tree::root("foo");
        assert_eq!(format!("{}", tree), "foo\n")
    }

    #[test]
    fn render_tree_with_leaves() {
        let tree = Tree::new(
            "foo", vec![
               Tree::new(
                   "bar", vec![
                    Tree::root("baz")
                   ]
               )
            ]
        );
        assert_eq!(format!("{}", tree), r#"foo
└── bar
    └── baz
"#)
    }
}
