// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::input::save_input,
    anyhow::{Context as _, Error, Result},
    fidl_fuchsia_fuzzer as fuzz, fuchsia_zircon_status as zx,
    futures::TryStreamExt,
    std::cell::RefCell,
    std::path::Path,
};

/// Returns which type of corpus is represented by the `fuchsia.fuzzer.Corpus` enum.
///
/// A seed corpus is immutable. A fuzzer can add or modify inputs in its live corpus.
pub fn get_type(seed: bool) -> fuzz::Corpus {
    if seed {
        fuzz::Corpus::Seed
    } else {
        fuzz::Corpus::Live
    }
}

/// Get the corresponding name for a `fuchsia.fuzzer.Corpus` enum.
pub fn get_name(corpus_type: fuzz::Corpus) -> &'static str {
    match corpus_type {
        fuzz::Corpus::Seed => "seed",
        fuzz::Corpus::Live => "live",
        other => unreachable!("unsupported type: {:?}", other),
    }
}

/// Basic corpus information returned by `read`.
#[derive(Debug, Default, PartialEq)]
pub struct Stats {
    pub num_inputs: u64,
    pub total_size: u64,
}

/// Receives and saves inputs from a corpus.
///
/// Takes a `stream` and serves `fuchsia.fuzzer.CorpusReader`. A fuzzer can publish a sequence of
/// test inputs using this protocol, typically in response to a `fuchsia.fuzzer.Controller/Fetch`
/// request or similar. The inputs are saved under `out_dir`, or in the current working directory
/// if `out_dir` is `None.
pub async fn read<P: AsRef<Path>>(
    stream: fuzz::CorpusReaderRequestStream,
    out_dir: P,
) -> Result<Stats> {
    // Without these `RefCell`s, the compiler will complain about references in the async block
    // below that escape the closure.
    let num_inputs: RefCell<u64> = RefCell::new(0);
    let total_size: RefCell<u64> = RefCell::new(0);
    stream
        .try_for_each(|request| async {
            match request {
                fuzz::CorpusReaderRequest::Next { test_input, responder } => {
                    {
                        let mut num_inputs = num_inputs.borrow_mut();
                        let mut total_size = total_size.borrow_mut();
                        *num_inputs += 1;
                        *total_size += test_input.size;
                    }
                    let result = match save_input(test_input, out_dir.as_ref()).await {
                        Ok(_) => zx::Status::OK,
                        Err(_) => zx::Status::IO,
                    };
                    responder.send(result.into_raw())
                }
            }
        })
        .await
        .map_err(Error::msg)
        .context("failed to handle fuchsia.fuzzer.CorpusReader request")?;
    let num_inputs = num_inputs.borrow();
    let total_size = total_size.borrow();
    Ok(Stats { num_inputs: *num_inputs, total_size: *total_size })
}

#[cfg(test)]
mod tests {
    use {
        super::{get_name, get_type, read, Stats},
        crate::input::InputPair,
        crate::util::digest_path,
        anyhow::{Error, Result},
        fidl_fuchsia_fuzzer as fuzz,
        fuchsia_fuzzctl_test::{verify_saved, Test},
        fuchsia_zircon_status as zx,
        futures::join,
    };

    // Writes a test input using the given `corpus_reader`.
    async fn send_one_input(corpus_reader: &fuzz::CorpusReaderProxy, data: Vec<u8>) -> Result<()> {
        let input_pair = InputPair::try_from_data(data)?;
        let (mut fidl_input, input) = input_pair.as_tuple();
        let (response, _) = futures::try_join!(
            async move { corpus_reader.next(&mut fidl_input).await.map_err(Error::msg) },
            input.send(),
        )?;
        zx::Status::ok(response).map_err(Error::msg)
    }

    #[test]
    fn test_get_type() -> Result<()> {
        assert_eq!(get_type(true), fuzz::Corpus::Seed);
        assert_eq!(get_type(false), fuzz::Corpus::Live);
        Ok(())
    }

    #[test]
    fn test_get_name() -> Result<()> {
        assert_eq!(get_name(fuzz::Corpus::Seed), "seed");
        assert_eq!(get_name(fuzz::Corpus::Live), "live");
        Ok(())
    }

    #[fuchsia::test]
    async fn test_read() -> Result<()> {
        let test = Test::try_new()?;
        let corpus_dir = test.create_dir("corpus")?;
        let corpus = vec![b"hello".to_vec(), b"world".to_vec(), b"".to_vec()];
        let cloned = corpus.clone();

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fuzz::CorpusReaderMarker>().unwrap();
        let read_fut = read(stream, &corpus_dir);
        let send_fut = || async move {
            for input in corpus.iter() {
                send_one_input(&proxy, input.to_vec()).await?;
            }
            Ok::<(), Error>(())
        };
        let send_fut = send_fut();
        let results = join!(read_fut, send_fut);
        assert_eq!(results.0.ok(), Some(Stats { num_inputs: 3, total_size: 10 }));
        assert!(results.1.is_ok());
        for input in cloned.iter() {
            let saved = digest_path(&corpus_dir, None, input);
            verify_saved(&saved, input)?;
        }
        Ok(())
    }
}
