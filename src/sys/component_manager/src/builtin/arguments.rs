// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::capability::BuiltinCapability,
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl_fuchsia_boot as fboot,
    fidl_fuchsia_io::FileProxy,
    fuchsia_zbi::{ZbiParser, ZbiResult, ZbiType},
    fuchsia_zircon_status::Status,
    futures::prelude::*,
    io_util::{file, file::ReadError, node::OpenError, OPEN_RIGHT_READABLE},
    lazy_static::lazy_static,
    routing::capability_source::InternalCapability,
    std::{
        collections::{hash_map::Iter, HashMap},
        env,
        sync::Arc,
    },
};

lazy_static! {
    static ref BOOT_ARGS_CAPABILITY_NAME: CapabilityName = "fuchsia.boot.Arguments".into();
}

// TODO(fxb/91451): Rename this file.
const BOOT_CONFIG_FILE: &str = "/boot/config/devmgr";

struct Env {
    vars: HashMap<String, String>,
}

impl Env {
    pub fn new() -> Self {
        /*
         * env::var() returns the first element in the environment.
         * We want to return the last one, so that booting with a commandline like
         * a=1 a=2 a=3 yields a=3.
         */
        let mut map = HashMap::new();
        for (k, v) in env::vars() {
            map.insert(k, v);
        }
        Env { vars: map }
    }

    #[cfg(test)]
    pub fn mock_new(map: HashMap<String, String>) -> Self {
        Env { vars: map }
    }
}

pub struct Arguments {
    vars: HashMap<String, String>,
}

impl Arguments {
    pub async fn new(parser: &mut Option<ZbiParser>) -> Result<Arc<Self>, Error> {
        let (cmdline_args, image_args) = match parser {
            Some(parser) => {
                let cmdline_args = match parser.try_get_item(ZbiType::Cmdline) {
                    Ok(result) => {
                        let _ = parser.release_item(ZbiType::Cmdline);
                        Some(result)
                    }
                    Err(_) => None,
                };

                let image_args = match parser.try_get_item(ZbiType::ImageArgs) {
                    Ok(result) => {
                        let _ = parser.release_item(ZbiType::ImageArgs);
                        Some(result)
                    }
                    Err(_) => None,
                };

                (cmdline_args, image_args)
            }
            None => (None, None),
        };

        // This config file may not be present depending on the device, but errors besides file
        // not found should be surfaced.
        let config = match file::open_in_namespace(BOOT_CONFIG_FILE, OPEN_RIGHT_READABLE) {
            Ok(config) => Some(config),
            Err(OpenError::Namespace(Status::NOT_FOUND)) => None,
            Err(err) => return Err(anyhow!("Failed to open {}: {}", BOOT_CONFIG_FILE, err)),
        };

        Arguments::new_from_sources(Env::new(), cmdline_args, image_args, config).await
    }

    async fn new_from_sources(
        env: Env,
        cmdline_args: Option<Vec<ZbiResult>>,
        image_args: Option<Vec<ZbiResult>>,
        config_file: Option<FileProxy>,
    ) -> Result<Arc<Self>, Error> {
        // There is an arbitrary (but consistent) ordering between these four sources, where
        // duplicate arguments in lower priority sources will be overwritten by arguments in
        // higher priority sources. Within one source derived from the ZBI such as cmdline_args,
        // the last time an argument occurs is canonically the chosen one.
        //
        // The chosen order is:
        // 1) Environment
        // 2) ZbiType::Cmdline
        // 3) ZbiType::ImageArgs
        // 4) Config file (hosted in bootfs)
        let mut result = HashMap::new();
        result.extend(env.vars);

        if cmdline_args.is_some() {
            for cmdline_arg_item in cmdline_args.unwrap() {
                let cmdline_arg_str = std::str::from_utf8(&cmdline_arg_item.bytes)
                    .context("failed to parse ZbiType::Cmdline as utf8")?;
                Arguments::parse_arguments(&mut result, cmdline_arg_str.to_string());
            }
        }

        if image_args.is_some() {
            for image_arg_item in image_args.unwrap() {
                let image_arg_str = std::str::from_utf8(&image_arg_item.bytes)
                    .context("failed to parse ZbiType::ImageArgs as utf8")?;
                Arguments::parse_legacy_arguments(&mut result, image_arg_str.to_string());
            }
        }

        if config_file.is_some() {
            // While this file has been "opened", FIDL I/O works on Fuchsia channels, so existence
            // isn't confirmed until an I/O operation is performed. As before, any errors besides
            // file not found should be surfaced.
            match file::read_to_string(&config_file.unwrap()).await {
                Ok(config) => Arguments::parse_legacy_arguments(&mut result, config),
                Err(ReadError::Fidl(fidl::Error::ClientChannelClosed {
                    status: Status::NOT_FOUND,
                    ..
                })) => (),
                Err(ReadError::Fidl(fidl::Error::ClientChannelClosed {
                    status: Status::PEER_CLOSED,
                    ..
                })) => (),
                Err(err) => return Err(anyhow!("Failed to read {}: {}", BOOT_CONFIG_FILE, err)),
            }
        }

        Ok(Arc::new(Self { vars: result }))
    }

    /// Arguments are whitespace separated.
    fn parse_arguments(parsed: &mut HashMap<String, String>, raw: String) {
        let lines = raw.trim_end_matches(char::from(0)).split_whitespace().collect::<Vec<&str>>();
        for line in lines {
            let split = line.splitn(2, "=").collect::<Vec<&str>>();
            if split.len() == 0 {
                println!("[Arguments] Empty argument string after parsing, ignoring: {}", line);
                continue;
            }

            if split[0].is_empty() {
                println!("[Arguments] Argument name cannot be empty, ignoring: {}", line);
                continue;
            }

            parsed.insert(
                split[0].to_string(),
                if split.len() == 1 { String::new() } else { split[1].to_string() },
            );
        }
    }

    /// Legacy arguments are newline separated, and allow comments.
    fn parse_legacy_arguments(parsed: &mut HashMap<String, String>, raw: String) {
        let lines = raw.trim_end_matches(char::from(0)).lines();
        for line in lines {
            let trimmed = line.trim_start().trim_end();

            if trimmed.starts_with("#") {
                // This is a comment.
                continue;
            }

            if trimmed.contains(char::is_whitespace) {
                // Leading and trailing whitespace have already been trimmed, so any other
                // internal whitespace makes this argument malformed.
                println!("[Arguments] Argument contains unexpected spaces, ignoring: {}", trimmed);
                continue;
            }

            let split = trimmed.splitn(2, "=").collect::<Vec<&str>>();
            if split.len() == 0 {
                println!("[Arguments] Empty argument string after parsing, ignoring: {}", trimmed);
                continue;
            }

            if split[0].is_empty() {
                println!("[Arguments] Argument name cannot be empty, ignoring: {}", trimmed);
                continue;
            }

            parsed.insert(
                split[0].to_string(),
                if split.len() == 1 { String::new() } else { split[1].to_string() },
            );
        }
    }

    fn get_bool_arg(self: &Arc<Self>, name: String, default: bool) -> bool {
        let mut ret = default;
        if let Ok(val) = self.var(name) {
            if val == "0" || val == "false" || val == "off" {
                ret = false;
            } else {
                ret = true;
            }
        }
        ret
    }

    fn var(&self, var: String) -> Result<&str, env::VarError> {
        if let Some(v) = self.vars.get(&var) {
            Ok(&v)
        } else {
            Err(env::VarError::NotPresent)
        }
    }

    fn vars<'a>(&'a self) -> Iter<String, String> {
        self.vars.iter()
    }
}

#[async_trait]
impl BuiltinCapability for Arguments {
    const NAME: &'static str = "Arguments";
    type Marker = fboot::ArgumentsMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fboot::ArgumentsRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                fboot::ArgumentsRequest::GetString { key, responder } => match self.var(key) {
                    Ok(val) => responder.send(Some(val)),
                    _ => responder.send(None),
                }?,
                fboot::ArgumentsRequest::GetStrings { keys, responder } => {
                    let mut vec = keys.into_iter().map(|x| self.var(x).ok());
                    responder.send(&mut vec)?
                }
                fboot::ArgumentsRequest::GetBool { key, defaultval, responder } => {
                    responder.send(self.get_bool_arg(key, defaultval))?
                }
                fboot::ArgumentsRequest::GetBools { keys, responder } => {
                    let mut iter =
                        keys.into_iter().map(|key| self.get_bool_arg(key.key, key.defaultval));
                    responder.send(&mut iter)?
                }
                fboot::ArgumentsRequest::Collect { prefix, responder } => {
                    let vec: Vec<_> = self
                        .vars()
                        .filter(|(k, _)| k.starts_with(&prefix))
                        .map(|(k, v)| k.to_owned() + "=" + &v)
                        .collect();
                    responder.send(&mut vec.iter().map(|x| x.as_str()))?
                }
            }
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&BOOT_ARGS_CAPABILITY_NAME)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        io_util::{directory, file::close, file::write, OPEN_RIGHT_WRITABLE},
        std::collections::HashMap,
    };

    fn serve_bootargs(args: Arc<Arguments>) -> Result<fboot::ArgumentsProxy, Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fboot::ArgumentsMarker>()?;
        fasync::Task::local(
            args.serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving arguments service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fuchsia::test]
    async fn malformed_argument_sources() {
        // 0xfe is an invalid UTF-8 byte, and all sources must be parsable as UTF-8.
        let data = vec![0xfe];

        let tempdir = tempfile::TempDir::new().unwrap();
        let dir = directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        let config = directory::open_file(
            &dir,
            "file",
            fidl_fuchsia_io::OPEN_RIGHT_WRITABLE | fidl_fuchsia_io::OPEN_FLAG_CREATE,
        )
        .await
        .unwrap();
        write(&config, data.clone()).await.unwrap();

        // Invalid config file.
        assert!(Arguments::new_from_sources(
            Env::mock_new(HashMap::new()),
            None,
            None,
            Some(config)
        )
        .await
        .is_err());

        // Invalid cmdline args.
        assert!(Arguments::new_from_sources(
            Env::mock_new(HashMap::new()),
            Some(vec![ZbiResult { bytes: data.clone(), extra: 0 }]),
            None,
            None
        )
        .await
        .is_err());

        // Invalid image args.
        assert!(Arguments::new_from_sources(
            Env::mock_new(HashMap::new()),
            None,
            Some(vec![ZbiResult { bytes: data.clone(), extra: 0 }]),
            None
        )
        .await
        .is_err());
    }

    #[fuchsia::test]
    async fn prioritized_argument_sources() {
        // Four arguments, all with the lowest priority.
        let env = Env::mock_new(
            [("arg1", "env1"), ("arg2", "env2"), ("arg3", "env3"), ("arg4", "env4")]
                .iter()
                .map(|(a, b)| (a.to_string(), b.to_string()))
                .collect(),
        );

        // Overrides three of the four arguments originally passed via environment variable. Note
        // that the second cmdline ZBI item overrides an argument in the first.
        let cmdline = vec![
            ZbiResult { bytes: b"arg2=notthisone arg3=cmd3 arg4=cmd4".to_vec(), extra: 0 },
            ZbiResult { bytes: b"arg2=cmd2".to_vec(), extra: 0 },
        ];

        // Overrides two of the three arguments passed via cmdline.
        let image_args = vec![ZbiResult { bytes: b"arg3=img3\narg4=img4".to_vec(), extra: 0 }];

        let tempdir = tempfile::TempDir::new().unwrap();
        let dir = directory::open_in_namespace(
            tempdir.path().to_str().unwrap(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .unwrap();

        // Finally, overrides one of the two arguments passed via image args. Note the comment
        // which is ignored.
        let config = directory::open_file(
            &dir,
            "file",
            fidl_fuchsia_io::OPEN_RIGHT_WRITABLE | fidl_fuchsia_io::OPEN_FLAG_CREATE,
        )
        .await
        .unwrap();

        // Write and flush to disk.
        write(&config, b"# Comment!\narg4=config4").await.unwrap();
        close(config).await.unwrap();

        let config =
            directory::open_file(&dir, "file", fidl_fuchsia_io::OPEN_RIGHT_READABLE).await.unwrap();

        let args = Arguments::new_from_sources(env, Some(cmdline), Some(image_args), Some(config))
            .await
            .unwrap();
        let proxy = serve_bootargs(args).unwrap();

        let result = proxy.get_string("arg1").await.unwrap().unwrap();
        assert_eq!(result, "env1");

        let result = proxy.get_string("arg2").await.unwrap().unwrap();
        assert_eq!(result, "cmd2");

        let result = proxy.get_string("arg3").await.unwrap().unwrap();
        assert_eq!(result, "img3");

        let result = proxy.get_string("arg4").await.unwrap().unwrap();
        assert_eq!(result, "config4");
    }

    #[fuchsia::test]
    async fn parse_argument_string() {
        let raw_arguments = "arg1=val1 arg3   arg4= =val2 arg5='abcd=defg'".to_string();
        let expected = [("arg1", "val1"), ("arg3", ""), ("arg4", ""), ("arg5", "'abcd=defg'")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();

        let mut actual = HashMap::new();
        Arguments::parse_arguments(&mut actual, raw_arguments);

        assert_eq!(actual, expected);
    }

    #[fuchsia::test]
    async fn parse_legacy_argument_string() {
        let raw_arguments = concat!(
            "arg1=val1\n",
            "arg2=val2,val3\n",
            "=AnInvalidEmptyArgumentName!\n",
            "perfectlyValidEmptyValue=\n",
            "justThisIsFineToo\n",
            "arg3=these=are=all=the=val\n",
            "  spacesAtStart=areFineButRemoved\n",
            "# This is a comment\n",
            "arg4=begrudinglyAllowButTrimTrailingSpaces \n"
        )
        .to_string();
        let expected = [
            ("arg1", "val1"),
            ("arg2", "val2,val3"),
            ("perfectlyValidEmptyValue", ""),
            ("justThisIsFineToo", ""),
            ("arg3", "these=are=all=the=val"),
            ("spacesAtStart", "areFineButRemoved"),
            ("arg4", "begrudinglyAllowButTrimTrailingSpaces"),
        ]
        .iter()
        .map(|(a, b)| (a.to_string(), b.to_string()))
        .collect();

        let mut actual = HashMap::new();
        Arguments::parse_legacy_arguments(&mut actual, raw_arguments);

        assert_eq!(actual, expected);
    }

    #[fuchsia::test]
    async fn can_get_string() -> Result<(), Error> {
        // check get_string works
        let vars: HashMap<String, String> =
            [("test_arg_1", "hello"), ("test_arg_2", "another var"), ("empty.arg", "")]
                .iter()
                .map(|(a, b)| (a.to_string(), b.to_string()))
                .collect();
        let proxy = serve_bootargs(
            Arguments::new_from_sources(Env::mock_new(vars), None, None, None).await?,
        )?;

        let res = proxy.get_string("test_arg_1").await?;
        assert_ne!(res, None);
        assert_eq!(res.unwrap(), "hello");

        let res = proxy.get_string("test_arg_2").await?;
        assert_ne!(res, None);
        assert_eq!(res.unwrap(), "another var");

        let res = proxy.get_string("empty.arg").await?;
        assert_ne!(res, None);
        assert_eq!(res.unwrap(), "");

        let res = proxy.get_string("does.not.exist").await?;
        assert_eq!(res, None);
        Ok(())
    }

    #[fuchsia::test]
    async fn can_get_strings() -> Result<(), Error> {
        // check get_strings() works
        let vars: HashMap<String, String> =
            [("test_arg_1", "hello"), ("test_arg_2", "another var")]
                .iter()
                .map(|(a, b)| (a.to_string(), b.to_string()))
                .collect();
        let proxy = serve_bootargs(
            Arguments::new_from_sources(Env::mock_new(vars), None, None, None).await?,
        )?;

        let req = vec!["test_arg_1", "test_arg_2", "test_arg_3"];
        let res = proxy.get_strings(&mut req.iter().map(|x| *x)).await?;
        let panicker = || panic!("got None, expected Some(str)");
        assert_eq!(res[0].as_ref().unwrap_or_else(panicker), "hello");
        assert_eq!(res[1].as_ref().unwrap_or_else(panicker), "another var");
        assert_eq!(res[2], None);
        assert_eq!(res.len(), 3);

        let empty_vec = Vec::new();
        let res = proxy.get_strings(&mut empty_vec.into_iter()).await?;
        assert_eq!(res.len(), 0);
        Ok(())
    }

    #[fuchsia::test]
    async fn can_get_bool() -> Result<(), Error> {
        let vars: HashMap<String, String> = [
            ("zero", "0"),
            ("not_true", "false"),
            ("not_on", "off"),
            ("empty_but_true", ""),
            ("should_be_true", "hello there"),
            ("still_true", "no"),
        ]
        .iter()
        .map(|(a, b)| (a.to_string(), b.to_string()))
        .collect();
        // map of key => (defaultval, expectedval)
        let expected: Vec<(&str, bool, bool)> = vec![
            // check 0, false, off all return false:
            ("zero", true, false),
            ("zero", false, false),
            ("not_true", false, false),
            ("not_on", true, false),
            // check empty arguments return true
            ("empty_but_true", false, true),
            // check other values return true
            ("should_be_true", false, true),
            ("still_true", true, true),
            // check unspecified values return defaultval.
            ("not_specified", false, false),
            ("not_specified", true, true),
        ];
        let proxy = serve_bootargs(
            Arguments::new_from_sources(Env::mock_new(vars), None, None, None).await?,
        )?;

        for (var, default, correct) in expected.iter() {
            let res = proxy.get_bool(var, *default).await?;
            assert_eq!(
                res, *correct,
                "expect get_bool({}, {}) = {} but got {}",
                var, default, correct, res
            );
        }

        Ok(())
    }

    #[fuchsia::test]
    async fn can_get_bools() -> Result<(), Error> {
        let vars: HashMap<String, String> = [
            ("zero", "0"),
            ("not_true", "false"),
            ("not_on", "off"),
            ("empty_but_true", ""),
            ("should_be_true", "hello there"),
            ("still_true", "no"),
        ]
        .iter()
        .map(|(a, b)| (a.to_string(), b.to_string()))
        .collect();
        // map of key => (defaultval, expectedval)
        let expected: Vec<(&str, bool, bool)> = vec![
            // check 0, false, off all return false:
            ("zero", true, false),
            ("zero", false, false),
            ("not_true", false, false),
            ("not_on", true, false),
            // check empty arguments return true
            ("empty_but_true", false, true),
            // check other values return true
            ("should_be_true", false, true),
            ("still_true", true, true),
            // check unspecified values return defaultval.
            ("not_specified", false, false),
            ("not_specified", true, true),
        ];
        let proxy = serve_bootargs(
            Arguments::new_from_sources(Env::mock_new(vars), None, None, None).await?,
        )?;

        let mut req: Vec<fboot::BoolPair> = expected
            .iter()
            .map(|(key, default, _expected)| fboot::BoolPair {
                key: String::from(*key),
                defaultval: *default,
            })
            .collect();
        let mut cur = 0;
        for val in proxy.get_bools(&mut req.iter_mut()).await?.iter() {
            assert_eq!(
                *val, expected[cur].2,
                "get_bools() index {} returned {} but want {}",
                cur, val, expected[cur].2
            );
            cur += 1;
        }
        Ok(())
    }

    #[fuchsia::test]
    async fn can_collect() -> Result<(), Error> {
        let vars: HashMap<String, String> = [
            ("test.value1", "3"),
            ("test.value2", ""),
            ("testing.value1", "hello"),
            ("test.bool", "false"),
            ("another_test.value1", ""),
            ("armadillos", "off"),
        ]
        .iter()
        .map(|(a, b)| (a.to_string(), b.to_string()))
        .collect();
        let proxy = serve_bootargs(
            Arguments::new_from_sources(Env::mock_new(vars), None, None, None).await?,
        )?;

        let res = proxy.collect("test.").await?;
        let expected = vec!["test.value1=3", "test.value2=", "test.bool=false"];
        for val in expected.iter() {
            assert_eq!(
                res.contains(&String::from(*val)),
                true,
                "collect() is missing expected value {}",
                val
            );
        }
        assert_eq!(res.len(), expected.len());

        let res = proxy.collect("nothing").await?;
        assert_eq!(res.len(), 0);

        Ok(())
    }
}
