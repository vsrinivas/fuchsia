#![feature(test)]

extern crate pretty;
extern crate tempfile;
extern crate test;
extern crate typed_arena;

use std::io;

use pretty::BoxAllocator;
use trees::Tree;
use typed_arena::Arena;

#[path = "../examples/trees.rs"]
mod trees;

macro_rules! bench_trees {
    ($b:expr, $out:expr, $allocator:expr, $size:expr) => {{
        let arena = Arena::new();
        let b = $b;
        let mut out = $out;
        let size = $size;

        let mut example = Tree::node("aaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

        for _ in 0..size {
            let bbbbbbs = arena.alloc_extend([example, Tree::node("dd")].iter().cloned());

            let ffffs = arena.alloc_extend(
                [Tree::node("gg"), Tree::node("hhh"), Tree::node("ii")]
                    .iter()
                    .cloned(),
            );

            let aaas = arena.alloc_extend(
                [
                    Tree::node_with_forest("bbbbbb", bbbbbbs),
                    Tree::node("eee"),
                    Tree::node_with_forest("ffff", ffffs),
                ].iter()
                    .cloned(),
            );

            example = Tree::node_with_forest("aaa", aaas);
        }

        let allocator = $allocator;

        b.iter(|| {
            example
                .pretty::<_, ()>(&allocator)
                .1
                .render(70, &mut out)
                .unwrap();
        });
    }};
}

#[bench]
fn bench_sink_box(b: &mut test::Bencher) -> () {
    bench_trees!(b, io::sink(), BoxAllocator, 1)
}

#[bench]
fn bench_sink_arena(b: &mut test::Bencher) -> () {
    bench_trees!(b, io::sink(), Arena::new(), 1)
}

#[bench]
fn bench_vec_box(b: &mut test::Bencher) -> () {
    bench_trees!(b, Vec::new(), BoxAllocator, 1)
}

#[bench]
fn bench_vec_arena(b: &mut test::Bencher) -> () {
    bench_trees!(b, Vec::new(), Arena::new(), 1)
}

#[bench]
fn bench_io_box(b: &mut test::Bencher) -> () {
    let out = tempfile::tempfile().unwrap();
    bench_trees!(b, io::BufWriter::new(out), BoxAllocator, 1)
}

#[bench]
fn bench_io_arena(b: &mut test::Bencher) -> () {
    let out = tempfile::tempfile().unwrap();
    bench_trees!(b, io::BufWriter::new(out), Arena::new(), 1)
}

#[bench]
fn bench_large_sink_box(b: &mut test::Bencher) -> () {
    bench_trees!(b, io::sink(), BoxAllocator, 50)
}

#[bench]
fn bench_large_sink_arena(b: &mut test::Bencher) -> () {
    bench_trees!(b, io::sink(), Arena::new(), 50)
}

#[bench]
fn bench_large_vec_box(b: &mut test::Bencher) -> () {
    bench_trees!(b, Vec::new(), BoxAllocator, 50)
}

#[bench]
fn bench_large_vec_arena(b: &mut test::Bencher) -> () {
    bench_trees!(b, Vec::new(), Arena::new(), 50)
}

#[bench]
fn bench_large_io_box(b: &mut test::Bencher) -> () {
    let out = tempfile::tempfile().unwrap();
    bench_trees!(b, io::BufWriter::new(out), BoxAllocator, 50)
}

#[bench]
fn bench_large_io_arena(b: &mut test::Bencher) -> () {
    let out = tempfile::tempfile().unwrap();
    bench_trees!(b, io::BufWriter::new(out), Arena::new(), 50)
}
