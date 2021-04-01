#![feature(test)]

extern crate test;

use futures_lite::*;
use tokio::runtime::Runtime;
use tokio::task;

const TASKS: usize = 300;
const STEPS: usize = 300;
const LIGHT_TASKS: usize = 25_000;

#[bench]
fn spawn_one(b: &mut test::Bencher) {
    let mut rt = Runtime::new().unwrap();
    b.iter(move || {
        rt.block_on(async { task::spawn(async {}).await.ok() });
    });
}

#[bench]
fn spawn_many(b: &mut test::Bencher) {
    let mut rt = Runtime::new().unwrap();
    b.iter(move || {
        rt.block_on(async {
            let mut tasks = Vec::new();
            for _ in 0..LIGHT_TASKS {
                tasks.push(task::spawn(async {}));
            }
            for task in tasks {
                task.await.ok();
            }
        });
    });
}

#[bench]
fn spawn_recursively(b: &mut test::Bencher) {
    fn go(i: usize) -> impl Future<Output = ()> + Send + 'static {
        async move {
            if i != 0 {
                task::spawn(async move {
                    let fut = go(i - 1).boxed();
                    fut.await;
                })
                .await
                .ok();
            }
        }
    }

    let mut rt = Runtime::new().unwrap();
    b.iter(move || {
        rt.block_on(async {
            let mut tasks = Vec::new();
            for _ in 0..TASKS {
                tasks.push(task::spawn(go(STEPS)));
            }
            for task in tasks {
                task.await.ok();
            }
        });
    });
}

#[bench]
fn yield_now(b: &mut test::Bencher) {
    let mut rt = Runtime::new().unwrap();
    b.iter(move || {
        rt.block_on(async {
            let mut tasks = Vec::new();
            for _ in 0..TASKS {
                tasks.push(task::spawn(async move {
                    for _ in 0..STEPS {
                        future::yield_now().await;
                    }
                }));
            }
            for task in tasks {
                task.await.ok();
            }
        });
    });
}
