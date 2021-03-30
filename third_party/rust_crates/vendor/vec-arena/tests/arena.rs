use vec_arena::Arena;

#[test]
fn new() {
    let arena = Arena::<i32>::new();
    assert!(arena.is_empty());
    assert_eq!(arena.len(), 0);
    assert_eq!(arena.capacity(), 0);
}

#[test]
fn insert() {
    let mut arena = Arena::new();

    for i in 0..10 {
        assert_eq!(arena.insert(i * 10), i);
        assert_eq!(arena[i], i * 10);
    }
    assert!(!arena.is_empty());
    assert_eq!(arena.len(), 10);
}

#[test]
fn with_capacity() {
    let mut arena = Arena::with_capacity(10);
    assert_eq!(arena.capacity(), 10);

    for _ in 0..10 {
        arena.insert(());
    }
    assert_eq!(arena.len(), 10);
    assert_eq!(arena.capacity(), 10);

    arena.insert(());
    assert_eq!(arena.len(), 11);
    assert!(arena.capacity() > 10);
}

#[test]
fn remove() {
    let mut arena = Arena::new();

    assert_eq!(arena.insert(0), 0);
    assert_eq!(arena.insert(10), 1);
    assert_eq!(arena.insert(20), 2);
    assert_eq!(arena.insert(30), 3);
    assert_eq!(arena.len(), 4);

    assert_eq!(arena.remove(1), Some(10));
    assert_eq!(arena.remove(2), Some(20));
    assert_eq!(arena.len(), 2);

    assert!(arena.insert(-1) < 4);
    assert!(arena.insert(-1) < 4);
    assert_eq!(arena.len(), 4);

    assert_eq!(arena.remove(0), Some(0));
    assert!(arena.insert(-1) < 4);
    assert_eq!(arena.len(), 4);

    assert_eq!(arena.insert(400), 4);
    assert_eq!(arena.len(), 5);
}

#[test]
fn invalid_remove() {
    let mut arena = Arena::new();
    for i in 0..10 {
        arena.insert(i.to_string());
    }

    assert_eq!(arena.remove(7), Some("7".to_string()));
    assert_eq!(arena.remove(5), Some("5".to_string()));

    assert_eq!(arena.remove(!0), None);
    assert_eq!(arena.remove(10), None);
    assert_eq!(arena.remove(11), None);

    assert_eq!(arena.remove(5), None);
    assert_eq!(arena.remove(7), None);
}

#[test]
fn clear() {
    let mut arena = Arena::new();
    arena.insert(10);
    arena.insert(20);

    assert!(!arena.is_empty());
    assert_eq!(arena.len(), 2);

    let cap = arena.capacity();
    arena.clear();

    assert!(arena.is_empty());
    assert_eq!(arena.len(), 0);
    assert_eq!(arena.capacity(), cap);
}

#[test]
fn indexing() {
    let mut arena = Arena::new();

    let a = arena.insert(10);
    let b = arena.insert(20);
    let c = arena.insert(30);

    arena[b] += arena[c];
    assert_eq!(arena[a], 10);
    assert_eq!(arena[b], 50);
    assert_eq!(arena[c], 30);
}

#[test]
#[should_panic]
fn indexing_vacant() {
    let mut arena = Arena::new();

    let _ = arena.insert(10);
    let b = arena.insert(20);
    let _ = arena.insert(30);

    arena.remove(b);
    arena[b];
}

#[test]
#[should_panic]
fn invalid_indexing() {
    let mut arena = Arena::new();

    arena.insert(10);
    arena.insert(20);
    arena.insert(30);

    arena[100];
}

#[test]
fn get() {
    let mut arena = Arena::new();

    let a = arena.insert(10);
    let b = arena.insert(20);
    let c = arena.insert(30);

    *arena.get_mut(b).unwrap() += *arena.get(c).unwrap();
    assert_eq!(arena.get(a), Some(&10));
    assert_eq!(arena.get(b), Some(&50));
    assert_eq!(arena.get(c), Some(&30));

    arena.remove(b);
    assert_eq!(arena.get(b), None);
    assert_eq!(arena.get_mut(b), None);
}

#[test]
fn reserve() {
    let mut arena = Arena::new();
    arena.insert(1);
    arena.insert(2);

    arena.reserve(10);
    assert!(arena.capacity() >= 11);
}

#[test]
fn reserve_exact() {
    let mut arena = Arena::new();
    arena.insert(1);
    arena.insert(2);
    arena.reserve(10);
    assert!(arena.capacity() >= 11);
}

#[test]
fn iter() {
    let mut arena = Arena::new();
    let a = arena.insert(10);
    let b = arena.insert(20);
    let c = arena.insert(30);
    let d = arena.insert(40);

    arena.remove(b);

    let mut it = arena.iter();
    assert_eq!(it.next(), Some((a, &10)));
    assert_eq!(it.next(), Some((c, &30)));
    assert_eq!(it.next(), Some((d, &40)));
    assert_eq!(it.next(), None);
}

#[test]
fn iter_mut() {
    let mut arena = Arena::new();
    let a = arena.insert(10);
    let b = arena.insert(20);
    let c = arena.insert(30);
    let d = arena.insert(40);

    arena.remove(b);

    {
        let mut it = arena.iter_mut();
        assert_eq!(it.next(), Some((a, &mut 10)));
        assert_eq!(it.next(), Some((c, &mut 30)));
        assert_eq!(it.next(), Some((d, &mut 40)));
        assert_eq!(it.next(), None);
    }

    for (index, value) in &mut arena {
        *value += index;
    }

    let mut it = arena.iter_mut();
    assert_eq!(*it.next().unwrap().1, 10 + a);
    assert_eq!(*it.next().unwrap().1, 30 + c);
    assert_eq!(*it.next().unwrap().1, 40 + d);
    assert_eq!(it.next(), None);
}

#[test]
fn from_iter() {
    let arena: Arena<_> = [10, 20, 30, 40].iter().cloned().collect();

    let mut it = arena.iter();
    assert_eq!(it.next(), Some((0, &10)));
    assert_eq!(it.next(), Some((1, &20)));
    assert_eq!(it.next(), Some((2, &30)));
    assert_eq!(it.next(), Some((3, &40)));
    assert_eq!(it.next(), None);
}
