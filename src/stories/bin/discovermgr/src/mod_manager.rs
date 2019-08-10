// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::{AddModInfo, SuggestedAction, Suggestion},
        story_manager::StoryManager,
    },
    failure::{bail, Error},
    fidl_fuchsia_modular::{
        ExecuteStatus, FocusMod, PuppetMasterProxy, SetFocusState, StoryCommand,
        StoryPuppetMasterMarker,
    },
    futures::future::join_all,
    parking_lot::Mutex,
    std::{
        collections::{HashMap, HashSet},
        sync::Arc,
    },
};

type EntityReference = String;

/// Starts mods and tracks the entity references in order to reissue intents
/// when entities change.
pub struct ModManager {
    // Maps an entity reference to actions launched with it.
    pub(super) actions: HashMap<EntityReference, HashSet<AddModInfo>>,
    puppet_master: PuppetMasterProxy,
    story_manager: Arc<Mutex<StoryManager>>,
}

impl ModManager {
    pub fn new(puppet_master: PuppetMasterProxy, story_manager: Arc<Mutex<StoryManager>>) -> Self {
        ModManager { actions: HashMap::new(), puppet_master, story_manager }
    }

    pub async fn execute_suggestion(&mut self, suggestion: Suggestion) -> Result<(), Error> {
        match suggestion.action() {
            SuggestedAction::AddMod(action) => {
                // execute a mod suggestion
                self.execute_actions(vec![&action], /*focus=*/ true).await?;
                self.story_manager.lock().add_to_story_graph(&action);
                for param in action.intent().parameters() {
                    self.actions
                        .entry(param.entity_reference().to_string())
                        .or_insert(HashSet::new())
                        .insert(action.clone());
                }
            }

            SuggestedAction::RestoreStory(restore_story_info) => {
                let actions = self
                    .story_manager
                    .lock()
                    .restore_story_graph(restore_story_info.story_name.to_string())
                    .map(|module| {
                        AddModInfo::new(
                            module.last_intent.clone(),
                            Some(restore_story_info.story_name.clone()),
                            None,
                        )
                    })
                    .collect::<Vec<AddModInfo>>();
                self.execute_actions(
                    actions.iter().map(|action| action).collect(),
                    /*focus=*/ true,
                ).await?;
            }
        }
        Ok(())
    }

    pub async fn replace<'a>(&'a mut self, old: &'a str, new: &'a str) {
        let actions = self
            .actions
            .remove(old)
            .unwrap_or(HashSet::new())
            .into_iter()
            .map(|action| action.replace_reference_in_parameters(old, new))
            .collect::<HashSet<AddModInfo>>();
        join_all(
            actions.iter().map(|action| self.execute_actions(vec![action], /*focus=*/ false)),
        ).await;
        self.actions.insert(new.to_string(), actions);
    }

    async fn execute_actions<'a>(
        &'a self,
        actions: Vec<&'a AddModInfo>,
        focus: bool,
    ) -> Result<(), Error> {
        let (story_puppet_master, server_end) =
            fidl::endpoints::create_proxy::<StoryPuppetMasterMarker>()?;
        self.puppet_master.control_story(&actions[0].story_name(), server_end)?;
        let mut commands = actions
            .iter()
            .map(|&action| StoryCommand::AddMod(action.clone().into()))
            .collect::<Vec<StoryCommand>>();

        // TODO: story_manager to record the name of focused mod in a story.
        // Currently we just focus on the first mod in a story.
        if focus {
            commands.push(StoryCommand::SetFocusState(SetFocusState { focused: true }));
            commands.push(StoryCommand::FocusMod(FocusMod {
                mod_name: vec![],
                mod_name_transitional: Some(actions[0].mod_name.to_string()),
            }));
        }

        story_puppet_master.enqueue(&mut commands.iter_mut())?;
        let result = story_puppet_master.execute().await?;
        if result.status != ExecuteStatus::Ok {
            bail!(
                "Modular error status:{:?} message:{}",
                result.status,
                result.error_message.unwrap_or("none".to_string())
            );
        } else {
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{DisplayInfo, Intent},
            testing::puppet_master_fake::PuppetMasterFake,
        },
        fidl_fuchsia_modular::{IntentParameter, IntentParameterData, PuppetMasterMarker},
        fuchsia_async as fasync,
        maplit::{hashmap, hashset},
    };

    // Tests that the right action is executed for a suggestion.
    // For this Puppet master is mocked and on a call to execute the command
    // will be verified. It also ensures that the suggestion action is tracked
    // and associated to the entity reference for future re-issuance.
    #[fasync::run_singlethreaded(test)]
    async fn execute_suggestion() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called when the suggestion is executed.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 3);
            if let (
                StoryCommand::AddMod(add_mod),
                StoryCommand::SetFocusState(set_focus),
                StoryCommand::FocusMod(focus_mod),
            ) = (&commands[0], &commands[1], &commands[2])
            {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("peridot-ref".to_string()),
                    },])
                );
                assert!(set_focus.focused);
                assert_eq!(focus_mod.mod_name_transitional, add_mod.mod_name_transitional);
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(request_stream);
        let story_manager = Arc::new(Mutex::new(StoryManager::new()));
        let mut mod_manager = ModManager::new(puppet_master_client, story_manager);

        let suggestion = suggestion!(
            action = "PLAY_MUSIC",
            title = "Play music",
            parameters = [(name = "artist", entity_reference = "peridot-ref")],
            story = "story_name"
        );
        match suggestion.action() {
            SuggestedAction::AddMod(action) => {
                let action_clone = action.clone();
                mod_manager.execute_suggestion(suggestion).await?;
                // Assert the action and the reference that originated it are tracked.
                assert_eq!(
                    mod_manager.actions,
                    hashmap!("peridot-ref".to_string() => hashset!(action_clone))
                );
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }

        Ok(())
    }

    // Tests that stored actions are issued with the udpated entity reference
    // when replace is called. The old action will be tracked and associated to
    // the new reference.
    // For this Puppet master is mocked and on a call to execute the command
    // will be verified.
    #[fasync::run_singlethreaded(test)]
    async fn replace() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called with the action of the old reference but with
        // the replaced entity reference.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 1);
            if let StoryCommand::AddMod(add_mod) = &commands[0] {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(add_mod.mod_name_transitional, Some("mod_name".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("garnet-ref".to_string()),
                    },])
                );
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(request_stream);
        let story_manager = Arc::new(Mutex::new(StoryManager::new()));
        let mut mod_manager = ModManager::new(puppet_master_client, story_manager);

        // Set initial state. The actions here will be executed with the new
        // entity reference in the parameter.
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "peridot-ref");
        let action =
            AddModInfo::new(intent, Some("story_name".to_string()), Some("mod_name".to_string()));
        mod_manager.actions = hashmap!("peridot-ref".to_string() => hashset!(action));

        mod_manager.replace("peridot-ref", "garnet-ref").await;

        // Assert the action was replaced.
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "garnet-ref");
        let action =
            AddModInfo::new(intent, Some("story_name".to_string()), Some("mod_name".to_string()));
        assert_eq!(mod_manager.actions, hashmap!("garnet-ref".to_string() => hashset!(action)));

        Ok(())
    }
}
