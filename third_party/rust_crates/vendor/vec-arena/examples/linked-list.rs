use vec_arena::Arena;

/// The null index, akin to null pointers.
///
/// Just like a null pointer indicates an address no object is ever stored at,
/// the null index indicates an index no object is ever stored at.
///
/// Number `!0` is the largest possible value representable by `usize`.
const NULL: usize = !0;

struct Node<T> {
    /// Previous node in the list.
    prev: usize,

    /// Next node in the list.
    next: usize,

    /// Actual value stored in node.
    value: T,
}

struct List<T> {
    /// This is where nodes are stored.
    arena: Arena<Node<T>>,

    /// First node in the list.
    head: usize,

    /// Last node in the list.
    tail: usize,
}

impl<T> List<T> {
    /// Constructs a new, empty doubly linked list.
    fn new() -> Self {
        List {
            arena: Arena::new(),
            head: NULL,
            tail: NULL,
        }
    }

    /// Returns the number of elements in the list.
    fn len(&self) -> usize {
        self.arena.len()
    }

    /// Links nodes `a` and `b` together, so that `a` comes before `b` in the list.
    fn link(&mut self, a: usize, b: usize) {
        if a != NULL {
            self.arena[a].next = b;
        }
        if b != NULL {
            self.arena[b].prev = a;
        }
    }

    /// Appends `value` to the back of the list.
    fn push_back(&mut self, value: T) -> usize {
        let node = self.arena.insert(Node {
            prev: NULL,
            next: NULL,
            value: value,
        });

        let tail = self.tail;
        self.link(tail, node);

        self.tail = node;
        if self.head == NULL {
            self.head = node;
        }
        node
    }

    /// Pops and returns the value at the front of the list.
    fn pop_front(&mut self) -> T {
        let node = self.arena.remove(self.head).unwrap();

        self.link(NULL, node.next);
        self.head = node.next;
        if node.next == NULL {
            self.tail = NULL;
        }
        node.value
    }

    /// Removes the element specified by `index`.
    fn remove(&mut self, index: usize) -> T {
        let node = self.arena.remove(index).unwrap();

        self.link(node.prev, node.next);
        if self.head == index {
            self.head = node.next;
        }
        if self.tail == index {
            self.tail = node.prev;
        }

        node.value
    }
}

fn main() {
    let mut list = List::new();

    // The list is now [].

    let one = list.push_back(1);
    list.push_back(2);
    list.push_back(3);

    // The list is now [1, 2, 3].

    list.push_back(10);
    let twenty = list.push_back(20);
    list.push_back(30);

    // The list is now [1, 2, 3, 10, 20, 30].

    assert!(list.len() == 6);

    assert!(list.remove(one) == 1);
    assert!(list.remove(twenty) == 20);

    // The list is now [2, 3, 10, 30].

    assert!(list.len() == 4);

    assert!(list.pop_front() == 2);
    assert!(list.pop_front() == 3);
    assert!(list.pop_front() == 10);
    assert!(list.pop_front() == 30);

    // The list is now [].

    assert!(list.len() == 0);
}
