use vec_arena::Arena;

/// The null index, akin to null pointers.
///
/// Just like a null pointer indicates an address no object is ever stored at,
/// the null index indicates an index no object is ever stored at.
///
/// Number `!0` is the largest possible value representable by `usize`.
const NULL: usize = !0;

struct Node<T> {
    /// Parent node.
    parent: usize,

    /// Left and right child.
    children: [usize; 2],

    /// Actual value stored in node.
    value: T,
}

impl<T> Node<T> {
    fn new(value: T) -> Node<T> {
        Node {
            parent: NULL,
            children: [NULL, NULL],
            value: value,
        }
    }
}

struct Splay<T> {
    /// This is where nodes are stored.
    arena: Arena<Node<T>>,

    /// The root node.
    root: usize,
}

impl<T: Ord> Splay<T> {
    /// Constructs a new, empty splay tree.
    fn new() -> Splay<T> {
        Splay {
            arena: Arena::new(),
            root: NULL,
        }
    }

    /// Links nodes `p` and `c` as parent and child with the specified direction.
    #[inline(always)]
    fn link(&mut self, p: usize, c: usize, dir: usize) {
        self.arena[p].children[dir] = c;
        if c != NULL {
            self.arena[c].parent = p;
        }
    }

    /// Performs a rotation on node `c`, whose parent is node `p`.
    #[inline(always)]
    fn rotate(&mut self, p: usize, c: usize) {
        // Variables:
        // - `c` is the child node
        // - `p` is it's parent
        // - `g` is it's grandparent

        // Find the grandparent.
        let g = self.arena[p].parent;

        // The direction of p-c relationship.
        let dir = if self.arena[p].children[0] == c { 0 } else { 1 };

        // This is the child of `c` that needs to be reassigned to `p`.
        let t = self.arena[c].children[dir ^ 1];

        self.link(p, t, dir);
        self.link(c, p, dir ^ 1);

        if g == NULL {
            // There is no grandparent, so `c` becomes the root.
            self.root = c;
            self.arena[c].parent = NULL;
        } else {
            // Link `g` and `c` together.
            let dir = if self.arena[g].children[0] == p { 0 } else { 1 };
            self.link(g, c, dir);
        }
    }

    /// Splays a node, rebalancing the tree in process.
    fn splay(&mut self, c: usize) {
        loop {
            // Variables:
            // - `c` is the current node
            // - `p` is it's parent
            // - `g` is it's grandparent

            // Find the parent.
            let p = self.arena[c].parent;
            if p == NULL {
                // There is no parent. That means `c` is the root.
                break;
            }

            // Find the grandparent.
            let g = self.arena[p].parent;
            if g == NULL {
                // There is no grandparent. Just one more rotation is left.
                // Zig step.
                self.rotate(p, c);
                break;
            }

            if (self.arena[g].children[0] == p) == (self.arena[p].children[0] == c) {
                // Zig-zig step.
                self.rotate(g, p);
                self.rotate(p, c);
            } else {
                // Zig-zag step.
                self.rotate(p, c);
                self.rotate(g, c);
            }
        }
    }

    /// Inserts a new node with specified `value`.
    fn insert(&mut self, value: T) {
        // Variables:
        // - `n` is the new node
        // - `p` will be it's parent
        // - `c` is the present child of `p`

        let n = self.arena.insert(Node::new(value));

        if self.root == NULL {
            self.root = n;
        } else {
            let mut p = self.root;
            loop {
                // Decide whether to go left or right.
                let dir = if self.arena[n].value < self.arena[p].value {
                    0
                } else {
                    1
                };
                let c = self.arena[p].children[dir];

                if c == NULL {
                    self.link(p, n, dir);
                    self.splay(n);
                    break;
                }
                p = c;
            }
        }
    }

    /// Pretty-prints the subtree rooted at `node`, indented by `indent` spaces.
    fn print(&self, node: usize, indent: usize)
    where
        T: std::fmt::Display,
    {
        if node != NULL {
            // Print the left subtree.
            self.print(self.arena[node].children[0], indent + 3);

            // Print the current node.
            println!("{:width$}{}", "", self.arena[node].value, width = indent);

            // Print the right subtree.
            self.print(self.arena[node].children[1], indent + 3);
        }
    }
}

fn main() {
    let mut splay = Splay::new();

    // Insert a bunch of pseudorandom numbers.
    let mut num = 1u32;
    for _ in 0..30 {
        num = num.wrapping_mul(17).wrapping_add(255);
        splay.insert(num);
    }

    // Display the whole splay tree.
    splay.print(splay.root, 0);
}
