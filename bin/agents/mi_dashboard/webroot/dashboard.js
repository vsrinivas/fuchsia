const RECONNECT_INTERVAL = 500;
const MAX_RECONNECT_INTERVAL = 2000;

var _activeAskQueryFlag = false;
var _entities = {};
var _focusedStoryId = null;
var _modules = {};
var _reconnectInterval = RECONNECT_INTERVAL;
var _stories = {};
var _toolbar = null;
var _tabBar = null;
var _webSocket = null;

$(function() {
  mdc.autoInit();
  _toolbar =
    new mdc.toolbar.MDCToolbar(document.querySelector('.mdc-toolbar'));
  _tabBar =
    new mdc.tabs.MDCTabBar(document.querySelector('#dashboard-tab-bar'));

  _tabBar.listen('MDCTabBar:change', function (t) {
      var newPanelSelector = _tabBar.activeTab.root_.hash;
      updateTabPanel(newPanelSelector);
    });

  _tabBar.layout();

  connectWebSocket();
})

function updateTabPanel(newPanelSelector) {
  var activePanel = document.querySelector('.panel.active');
  if (activePanel) {
    activePanel.classList.remove('active');
  }
  var newActivePanel = document.querySelector(newPanelSelector);
  if (newActivePanel) {
    newActivePanel.classList.add('active');
  }
}

function connectWebSocket() {
  _webSocket = new WebSocket("ws://" + window.location.host + "/ws");
  _webSocket.onopen = handleWebSocketOpen;
  _webSocket.onerror = handleWebSocketError;
  _webSocket.onclose = handleWebSocketClose;
  _webSocket.onmessage = handleWebSocketMessage;
}

function handleWebSocketOpen(evt) {
  $("#connectedLabel").text("Connected");
  // reset reconnect
  _reconnectInterval = RECONNECT_INTERVAL;
}

function handleWebSocketError(evt) {
  console.log("WebSocket Error: " + evt.toString());
}

function handleWebSocketClose(evt) {
  $("#connectedLabel").text("Disconnected");

  // attempt to reconnect
  attemptReconnect();
}

function handleWebSocketMessage(evt) {
  // parse the JSON message
  var message = JSON.parse(evt.data);
  if ("context.values" in message) {
    handleContextUpdate(message["context.values"]);
  }
  if ("context.subscriptions" in message) {
    handleContextSubscribers(message["context.subscriptions"]);
  }
  if ("suggestions" in message) {
    handleSuggestionsUpdate(message["suggestions"]);
  }
  if ("action_log.all" in message) {
    handleActionLogReset(message["action_log.all"]);
  }
  if ("action_log.new_action" in message) {
    handleActionLogAdd(message["action_log.new_action"]);
  }
}

function makeProposalHtml(proposal) {
  var item = document.createElement("li");
  var headline = document.createElement("span");
  var subheadline = document.createElement("span");
  $(item).addClass("mdc-list-item").addClass("proposal-item");
  $(headline)
      .addClass("mdc-list-item__text")
      .text(proposal.display.headline);
  $(subheadline)
      .addClass("mdc-list-item__text__secondary")
      .text(proposal.publisherUrl);
  $(headline).append(subheadline);
  $(item).append(headline);
  return item;
}

function updateProposals(cardId, proposals) {
  // TODO(andrewosh): really should do some DOM diffing here.
  var cardList = $(cardId);
  cardList.empty();
  proposals.forEach(function (proposal) {
    cardList.append(makeProposalHtml(proposal));
  });
}

function updateLastSelection(selection) {
  var selectionList = $("#lastSelection");
  selectionList.empty();
  selectionList.append(makeProposalHtml(selection));
}

function updateLastQuery(query) {
  var queryElem = $("#lastQuery");
  queryElem.text(query);
}

function updateAgentProposals(proposals) {
  proposals.forEach(function(proposal) {
    var agentElems = getOrCreateAgentElements(proposal.publisherUrl);
    addProposalToAgent(proposal,agentElems);
  });
}

function handleSuggestionsUpdate(suggestions) {
  updateProposals('#askProposals', suggestions.ask_proposals);
  updateProposals('#nextProposals', suggestions.next_proposals);
  if (_activeAskQueryFlag) {
    updateProposals('#askSuggestionsOverview', suggestions.ask_proposals);
  }
  if (suggestions.selection) {
    updateLastSelection(suggestions.selection);
  }
  updateLastQuery(suggestions.ask_query);

  // Update agents with next proposals
  clearAgentProposals();
  updateAgentProposals(suggestions.next_proposals);
}

function makeContextTopicRow(topic,topicValue) {
  var topicId = "topic-" + topic;
  var row = $("<tr/>")
    .append($("<td/>").addClass('wrappable').text(topic))
    .append($("<td/>").attr("topic",topic)
      .append($("<pre/>").text(topicValue)));

  return row;
}

function makeContextRowId(id) {
  var elementId = "context-" + id;
  return $.escapeSelector(elementId);
}

function findContextRow(id) {
  var valueRows = $("#context li[id^='" + makeContextRowId(id) + "']");
  if (valueRows.length == 0) return null;
  return valueRows[0];
}

function contextTypeToString(type) {
  switch (type) {
    case 1: return "STORY";
    case 2: return "MODULE";
    case 3: return "AGENT";
    case 4: return "ENTITY";
    default: return "???";
  }
}

function contextValueMetadataToList(meta) {
  ret = [];
  if (meta.story) {
    if (meta.story.id) {
      ret.push(["story", "id", meta.story.id]);
    }
    function focusedStateToString(state) {
      switch (state) {
        case 1: return "YES";
        case 2: return "NO";
      }
    }
    if (meta.story.focused) {
      ret.push(["story", "focused", focusedStateToString(meta.story.focused.state)]);
    }
  }
  if (meta.mod) {
    if (meta.mod.url) {
      ret.push(["module", "url", meta.mod.url]);
    }
    if (meta.mod.path) {
      ret.push(["module", "path", meta.mod.path.join(", ")]);
    }
  }
  if (meta.entity) {
    if (meta.entity.topic) {
      ret.push(["entity", "topic", meta.entity.topic]);
    }
    if (meta.entity.type) {
      ret.push(["entity", "type", meta.entity.type.join(", ")]);
    }
  }

  return ret;
}

function clientInfoToString(info) {
  return JSON.stringify(info);
}

function handleContextUpdate(context) {
  updateOverviewFromContext(context);

  // |context| is a list of elements, where each has a |parentIds| field.
  // Invert this so we have a map of values where the children are listed,
  // and a list of root elements.
  var contextTree = {};
  var rootValueIds = [];
  $.each(context, function(idx, value) {
    value.children = [];
    contextTree[value.id] = value;

    if (value.parentIds.length > 0) {
      $.each(value.parentIds, function(idx, parentId) {
        contextTree[parentId].children.push(value.id);
      });
    } else {
      rootValueIds.push(value.id);
    }
  });

  // Rebuild the context display.
  var buildValueDomRecursive = function(id) {
    var entry = contextTree[id];
    var div = $("<div/>");
    div.append($("<b/>").text(contextTypeToString(entry.value.type)));
    $.each(contextValueMetadataToList(entry.value.meta), function(idx, pair) {
      div.append("<br/>");
      div.append($("<span/>").html(pair[0] + "." + pair[1] + " = " + pair[2]));
    });
    if (entry.value.content != null) {
      try {
        div.append($("<pre/>").text(
          JSON.stringify(JSON.parse(entry.value.content), null, 2)));
      } catch (e) {
        div.append($("<pre/>").text(entry.value.content));
      }
    }
    $.each(entry.children, function(idx, childId) {
      div.append(buildValueDomRecursive(childId).addClass("context-indent"));
    });
    return div;
  };

  $("#context").empty();
  $.each(rootValueIds, function(idx, id) {
    buildValueDomRecursive(id).addClass("context-root-value").appendTo("#context");
  });
}

function handleContextSubscribers(subscribers) {
  $("#contextSubscriptions").empty();

  subscribers.forEach(function(update) {
    // Sample HTML to be created
    // <li class="mdc-list-item">
    //   <span class="mdc-list-item__text">
    //     <span class="agent-url">file:///system/apps/agents/module_suggester</span>
    //     <span class="mdc-list-item__text__secondary">raw/text</span>
    //   </span>
    // </li>
    // <li class="mdc-list-divider" role="separator"></li>
    var listElem = $('<div/>');

    // TODO(thatguy): Make the client identity more useful.
    listElem.append($("<div/>").addClass("subscriber")
                                .text(clientInfoToString(update.debugInfo.clientInfo)));
    $.each(update.query.selector, function(key, selector) {
      listElem.append(' ');
      $('<div/>')
        .addClass('mdc-list-item__text__secondary')
        .addClass('context-selector-key')
        .text(key + ":")
        .appendTo(listElem);
      $('<div/>')
        .addClass('mdc-list-item__text__secondary')
        .addClass('context-selector-type')
        .text("for type " + contextTypeToString(selector.type))
        .appendTo(listElem);
      var metadataValues = contextValueMetadataToList(selector.meta);
      $.each(metadataValues, function(idx, parts) {
        $("<div/>")
          .addClass('mdc-list-item__text__secondary')
          .addClass("context-selector-metadata")
          .text(parts[0] + "." + parts[1] + " == " + parts[2])
          .appendTo(listElem);
      });
    });

    $("#contextSubscriptions")
      .append($('<li/>').append(listElem))
      .append($('<li/>').addClass('mdc-list-divider').attr('role','divider'));

    /*
     * TODO(thatguy): Update this.
    // update agents page information
    if (update.subscriber.type == "agent") {
      var agentElems = getOrCreateAgentElements(update.subscriber.url);
      update.queries.forEach(function(query) {
        // TODO(jwnichols): Special behavior for subscribers to all topics?
        for(var i = 0; i < query.length; i++) {
          addAgentContextTopic(query[i],agentElems);
        }
      });
    }
    */
  });
}

function handleActionLogReset(actionList) {
  $("#actionLog").empty();

  actionList.forEach(handleActionLogAdd);
}

function handleActionLogAdd(action) {
  // <li class="mdc-list-item">
  //   <span class="mdc-list-item__text">
  //     Two-line item
  //     <span class="mdc-list-item__text__secondary">Secondary text</span>
  //   </span>
  // </li>

  // Assemble Action Log Panel Data
  var methodElem = $("<span/>")
    .addClass('mdc-list-item__text')
    .text(action.method);
  var componentUrlElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.component_url);
  var parametersElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.parameters);

  methodElem.append(' ')
    .append(componentUrlElem)
    .append(' ')
    .append(parametersElem);

  $("#actionLog")
    .prepend($('<li/>').addClass('mdc-list-item').append(methodElem))
    .prepend($('<li/>').addClass('mdc-list-divider').attr('role','divider'));

  // Assemble Action Log Overview Data
  methodElem = $('<span/>')
    .addClass('mdc-list-item__text')
    .text(action.method);
  parametersElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.parameters);

  methodElem.append(" ").append(parametersElem);
  $('#actionLogOverview')
    .prepend($('<li/>').addClass('mdc-list-item').append(methodElem))
    .prepend($('<li/>').addClass('mdc-list-divider').attr('role','divider'));
}

function attemptReconnect() {
  console.log("Attempting to reconnect after " + _reconnectInterval);

  // reconnect after the timeout
  setTimeout(connectWebSocket, _reconnectInterval);
  // exponential reconnect timeout
  var nextInterval = _reconnectInterval * 2;
  if (nextInterval < MAX_RECONNECT_INTERVAL) {
    _reconnectInterval = nextInterval;
  } else {
    _reconnectInterval = MAX_RECONNECT_INTERVAL;
  }
}

function updateOverviewFromContext(context) {}

function processComplexStoryTopic(storyId,complexTopic,rawValue) {
  var moduleRegex = /module\/([^\/]+)\/(.+)/;
  var moduleRegexResults = complexTopic.match(moduleRegex);

  if (moduleRegexResults != null && moduleRegexResults[1] != null) {
    var moduleHash = moduleRegexResults[1];
    var moduleTopic = moduleRegexResults[2];

    if (moduleTopic == 'meta') {
      // This topic contains information about the module
      updateModuleInStory(storyId,moduleHash,rawValue);
    } else if (moduleTopic.startsWith('explicit')) {
      // This topic contains informationa about a focal entity
      updateFocalEntityInStory(storyId,moduleHash,moduleTopic,rawValue);
    }
  } else {
    var linkRegex = /link\/(.+)/;
    var linkRegexResults = complexTopic.match(linkRegex);

    if (linkRegexResults != null && linkRegexResults[1] != null) {
      var entityElems = getOrCreateEntityOverviewElements(storyId,
                          null, // null indicates a context link entity
                          linkRegexResults[1]); // entity topic name
      setEntityValue(rawValue,entityElems);
    }
  }
}

function updateModuleInStory(storyId,moduleHash,rawData) {
  var moduleData = JSON.parse(rawData);
  var moduleElems = getOrCreateModuleOverviewElements(storyId,moduleHash);
  if (moduleData['url'] != null) {
    setModuleUrl(moduleData['url'],moduleElems);
  }
  if (moduleData['module_path'] != null) {
    var modulePath = moduleData['module_path'][0];
    for(var i = 1; i < moduleData['module_path'].length; i++) {
      modulePath += ' ' + moduleData['module_path'][i];
    }
    setModulePath(modulePath,moduleElems);
  }
}

function getOrCreateContextLinkElement(storyId) {
  var linkElemId = storyId + '-contextlink';
  // context link elems are stored alongside the modules
  var linkElems = _modules[linkElemId];
  if (linkElems == null) {
    // <li id="b234kj2jn5j34342l3k3-mdece" class="mdc-list-item module-list-item">
    //   <span class="module-url mdc-list-item__text">
    //     file:///system/apps/maxwell_btr
    //     <span class="module-path mdc-list-item__text__secondary">
    //       root
    //     </span>
    //   </span>
    // </li>
    var linkTitleElem = $('<span/>').addClass('module-url')
      .addClass('mdc-list-item__text')
      .text('context link');

    var linkListElem = $('<li/>').attr('id',linkElemId)
      .addClass('mdc-list-item')
      .addClass('module-list-item')
      .addClass('context-link-item')
      .append(linkTitleElem);

    var storyElems = getOrCreateStoryOverviewElements(storyId);
    // Insert the context link list item after the story list item
    storyElems.find('.story-list-item').after(linkListElem);

    linkElems = _modules[linkElemId] = linkListElem;
  }
  return linkElems;
}

function getOrCreateModuleOverviewElements(storyId,moduleHash) {
  var moduleElemId = storyId + '-' + moduleHash;
  var moduleElems = _modules[moduleElemId];
  if (moduleElems == null) {
    // <li id="b234kj2jn5j34342l3k3-mdece" class="mdc-list-item module-list-item">
    //   <span class="module-url mdc-list-item__text">
    //     file:///system/apps/maxwell_btr
    //     <span class="module-path mdc-list-item__text__secondary">
    //       root
    //     </span>
    //   </span>
    // </li>
    var moduleListElem = $('<li/>').attr('id',moduleElemId)
      .addClass('mdc-list-item')
      .addClass('module-list-item');

    var modulePathElem = $('<span/>').addClass('module-path')
      .addClass('mdc-list-item__text__secondary')
      .text('module-path');

    var moduleUrlElem = $('<span/>').addClass('module-url')
      .addClass('mdc-list-item__text')
      .text('file://a/module/url')
      .append(' ')
      .append(modulePathElem);

    moduleListElem.append(moduleUrlElem);

    var storyElems = getOrCreateStoryOverviewElements(storyId);
    storyElems.append(moduleListElem);

    moduleElems = _modules[moduleElemId] = moduleListElem;
  }
  return moduleElems;
}

function setModuleUrl(moduleUrl,moduleElems) {
  moduleElems.find('span')[0].firstChild.textContent = moduleUrl;
}

function setModulePath(modulePath,moduleElems) {
  moduleElems.find('span')[1].firstChild.textContent = modulePath;
}

function updateFocalEntityInStory(storyId,moduleHash,entityTopic,rawValue) {
  var entityTopicRegex = /explicit\/(.+)/;
  var entityTopicRegexResults = entityTopic.match(entityTopicRegex);

  if (entityTopicRegexResults != null && entityTopicRegexResults[1] != null) {
    var entityElems = getOrCreateEntityOverviewElements(storyId,
                        moduleHash,
                        entityTopicRegexResults[1]);
    setEntityValue(rawValue,entityElems);
  }
}

function getOrCreateEntityOverviewElements(storyId,moduleHash,entityTopic) {
  var entityElemId;
  if (moduleHash == null || moduleHash == 'contextlink') {
    entityElemId = storyId + '-contextlink-' + entityTopic;
  } else {
    entityElemId = storyId + '-' + moduleHash + '-' + entityTopic;
  }
  var entityElems = _entities[entityElemId];
  if (entityElems == null) {
    // <li id="b234kj2jn5j34342l3k3-mdece-raw/text" class="mdc-list-item entity-list-item">
    //   <span class="entity-name mdc-list-item__text">
    //     raw/text
    //     <span class="entity-value mdc-list-item__text__secondary">
    //       TODO: value
    //     </span>
    //   </span>
    // </li>
    var entityListElem = $('<li/>').attr('id',entityElemId)
      .addClass('mdc-list-item')
      .addClass('entity-list-item');

    var entityValueElem = $('<span/>').addClass('entity-value')
      .addClass('mdc-list-item__text__secondary')
      .text('entity value');

    var entityNameElem = $('<span/>').addClass('entity-name')
      .addClass('mdc-list-item__text')
      .text(entityTopic)
      .append(' ')
      .append(entityValueElem);

    entityListElem.append(entityNameElem);

    var moduleElems;
    if (moduleHash == null || moduleHash == 'contextlink') {
      moduleElems = getOrCreateContextLinkElement(storyId);
    } else {
      moduleElems = getOrCreateModuleOverviewElements(storyId,moduleHash);
    }
    entityListElem.insertAfter(moduleElems);

    entityElems = _entities[entityElemId] = entityListElem;
  }
  return entityElems;
}

function setEntityName(entityName,entityElems) {
  entityElems.find('span')[0].firstChild.textContent = entityName;
}

function setEntityValue(entityValue,entityElems) {
  entityElems.find('span')[1].firstChild.textContent = entityValue;
}

function getOrCreateStoryOverviewElements(storyId) {
  var storyElems = _stories[storyId];
  if (storyElems == null) {
    // we need to create the story elements
    // <div class="story-list-group">
    // <li id="b234kj2jn5j34342l3k3" class="mdc-list-item story-list-item story-visible">
    //   <span class="story-name mdc-list-item__text">
    //     Story Name
    //     <span class="story-id mdc-list-item__text__secondary">
    //       b234kj2jn5j34342l3k3
    //     </span>
    //   </span>
    // </li>
    // </div>
    var storyDivItem = $('<div/>').addClass('story-list-group')
      .attr('id',storyId);

    var storyListItem = $('<li/>').addClass('mdc-list-item')
      .addClass('story-list-item');

    var storyIdElem = $('<span/>').addClass('story-id')
      .addClass('mdc-list-item__text__secondary')
      .text(storyId);

    var storyNameElem = $('<span/>').addClass('story-name')
      .addClass('mdc-list-item__text')
      .text('Story Name')
      .append(' ')
      .append(storyIdElem);

    var divider = $('<li/>').addClass('mdc-list-divider').attr('role','divider');

    storyListItem.append(storyNameElem);
    storyDivItem.append(divider)
      .append(storyListItem);

    $('#story-overview-list').append(storyDivItem);

    storyElems = _stories[storyId] = storyDivItem;
  }
  return storyElems;
}

function setStoryName(storyName, storyElems) {
  storyElems.find('span')[0].firstChild.textContent = storyName;
}

function setStoryVisible(storyVisible, storyElems) {
  if (storyVisible) {
    storyElems.addClass('story-visible');
  } else {
    storyElems.removeClass('story-visible');
  }
}

function setStoryState(storyState, storyElems) {
  // TODO(jwnichols): Add story state to the overview
}

function getOrCreateAgentElements(agentUrl) {
  var agentCardId = 'agent-' + agentUrl;
  var agentElems = $('#' + $.escapeSelector(agentCardId));
  if (agentElems.length == 0) {
    agentElems = $(`<div class="cell mdc-layout-grid__cell agent-card">
        <div class="mdc-card">
          <section class="mdc-card__primary">
            <h1 class="mdc-card__title mdc-card__title--large wrappable"></h1>
          </section>
          <section class="mdc-card__supporting-text agent-context">
            <h2 class="mdc-card__subtitle agent-subhead">Subscribed Context Topics</h2>
            <table class="agent-context-table">
              <thead><tr><th>Topic</th><th>Value</th></tr></thead>
              <tbody>
              </tbody>
            </table>
            <h2 class="mdc-card__subtitle agent-subhead">Proposals Made</h2>
          </section>
          <ul class="mdc-list mdc-list--two-line mdc-list--dense">
          </ul>
        </div> <!-- mdc_card -->
      </div>`);

    agentElems.attr('id',agentCardId);
    agentElems.find('h1').text(agentUrl);

    $('#agent-card-addpoint').append(agentElems);
  }
  return agentElems;
}

function addAgentContextTopic(topic,agentElems) {
  var tbody = agentElems.find('tbody');
  var contextRow = agentElems.find("td[topic^='" + topic + "']");
  if (contextRow.length == 0) {
    tbody.append(makeContextTopicRow(topic,""));
  }
}

function addProposalToAgent(proposal,agentElems) {
  var proposalItem = makeProposalHtml(proposal);
  // The secondary headline by default is the URL, which is redundant
  // on the agent page.  Change it to the subheadline.
  $(proposalItem).find('span.mdc-list-item__text__secondary')
    .text(proposal.display.subheadline);
  agentElems.find('ul').prepend(proposalItem);
  // move this agent to the top
  $('#agent-card-addpoint').prepend(agentElems);
}

function clearAgentProposals() {
  $('#agents-panel ul').empty();
}
