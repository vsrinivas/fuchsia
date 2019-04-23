use cm_fidl_translator;
use failure::Error;
use fidl_fuchsia_data as fd;
use fidl_fuchsia_sys2::{
    Capability, ChildDecl, ChildId, ComponentDecl, DirectoryCapability, ExposeDecl, ExposeSource,
    OfferDecl, OfferSource, OfferTarget, SelfId, ServiceCapability, StartupMode, UseDecl,
};
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;

fn main() {
    let cm_content = read_cm("/pkg/meta/example.cm").expect("could not open example.cm");
    let golden_cm = read_cm("/pkg/data/golden.cm").expect("could not open golden.cm");
    assert_eq!(&cm_content, &golden_cm);

    let cm_decl = cm_fidl_translator::translate(&cm_content).expect("could not translate cm");
    let expected_decl = {
        let program = fd::Dictionary {
            entries: vec![fd::Entry {
                key: "binary".to_string(),
                value: Some(Box::new(fd::Value::Str("bin/example".to_string()))),
            }],
        };
        let uses = vec![UseDecl {
            capability: Some(Capability::Service(ServiceCapability {
                path: Some("/fonts/CoolFonts".to_string()),
            })),
            target_path: Some("/svc/fuchsia.fonts.Provider".to_string()),
        }];
        let exposes = vec![ExposeDecl {
            capability: Some(Capability::Directory(DirectoryCapability {
                path: Some("/volumes/blobfs".to_string()),
            })),
            source: Some(ExposeSource::Myself(SelfId {})),
            target_path: Some("/volumes/blobfs".to_string()),
        }];
        let offers = vec![OfferDecl {
            capability: Some(Capability::Service(ServiceCapability {
                path: Some("/svc/fuchsia.logger.Log".to_string()),
            })),
            source: Some(OfferSource::Child(ChildId { name: Some("logger".to_string()) })),
            targets: Some(vec![OfferTarget {
                target_path: Some("/svc/fuchsia.logger.Log".to_string()),
                child_name: Some("netstack".to_string()),
            }]),
        }];
        let children = vec![
            ChildDecl {
                name: Some("logger".to_string()),
                uri: Some("fuchsia-pkg://fuchsia.com/logger/stable#meta/logger.cm".to_string()),
                startup: Some(StartupMode::Lazy),
            },
            ChildDecl {
                name: Some("netstack".to_string()),
                uri: Some("fuchsia-pkg://fuchsia.com/netstack/stable#meta/netstack.cm".to_string()),
                startup: Some(StartupMode::Lazy),
            },
        ];
        let facets = fd::Dictionary {
            entries: vec![
                fd::Entry {
                    key: "author".to_string(),
                    value: Some(Box::new(fd::Value::Str("Fuchsia".to_string()))),
                },
                fd::Entry { key: "year".to_string(), value: Some(Box::new(fd::Value::Inum(2018))) },
            ],
        };
        ComponentDecl {
            program: Some(program),
            uses: Some(uses),
            exposes: Some(exposes),
            offers: Some(offers),
            children: Some(children),
            facets: Some(facets),
        }
    };
    assert_eq!(cm_decl, expected_decl);
}

fn read_cm(file: &str) -> Result<String, Error> {
    let mut buffer = String::new();
    let path = PathBuf::from(file);
    File::open(&path)?.read_to_string(&mut buffer)?;
    Ok(buffer)
}
