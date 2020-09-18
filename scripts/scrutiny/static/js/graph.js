/**
 * @fileoverview Implements the interactive parsing of component data and
 * drawing it to the UI.
 */

 function defaultColorFill(e) {
  if (e.source == "bootfs") {
    return "#FFA726";
  }
  if (e.source == "unknown") {
    return "#9C27B0";
  }
  if (e.routes.offers.length > 0) {
    return "#FF0266";
  }
  if (e.routes.uses.length > 0) {
    return "#1DE9B6";
  }
  return "#09A4AE";
}

function searchColorFill(e) {
  if (e == "node") {
    return "#FF0266";
  } else if (e == "uses") {
    return "#1DE9B6";
  } else if (e == "used_by") {
    return "#9C27B0";
  } else {
    return "#09A4AE";
  }
}

/**
 * Simple wrapper around the component mapper json API.
 */
class ComponentMapperApi {
  constructor(baseUrl) {
    this.baseUrl = baseUrl;

    this.routesUrl = baseUrl + "api/routes";
    this.componentsUrl = baseUrl + "api/components";
    this.manifestUrl = baseUrl + "api/component/manifest";
  }

  /**
   * Takes a component and transforms it into a node as expected
   * by the graph structure.
   */
  makeNode(component) {
    // FIXME: Make this a lazy load, since thousands of concurrent requests seem to cause issues in JS.
    let mani = {}; //this.post_request(this.manifestUrl, JSON.stringify({component_id: component.id}));
    let metaInd = component.url.lastIndexOf("#meta/");
    let componentName = metaInd == -1 ? component.url : component.url.slice(metaInd + 6);
    let node = {
      consumers: 0,
      id: component.id,
      url: component.url,
      manifest: mani,
      name: componentName,
      routes: {
        exposes: [], // FIXME: Is this ever used?
        offers: [], // Empty vector until filled out via makeLink
        uses: [], // Empty vector until filled out via makeLink.
      },
      source: "package",
      version: component.version,
    }
    return [component.id, node];
  }

  /**
   * Takes a route and transforms it into a dictionary as expected
   * by the graph structure.
   */
  makeLink(route) {
    var src = this.nodes[route.src_id];
    var dst = this.nodes[route.dst_id];

    if (src == null || dst == null) {
      console.log("Unable to resolve route node ids: " + route.src_id + "->" + route.dst_id);
    }

    var link = {
      fidl_service: route.service_name,
      source: src["id"],
      target: dst["id"],
      type: "use"
    };

    // Update the list of uses, number of consumers, and offerings for src and dst nodes.
    if (src != null && dst != null) {
      src.routes.uses.push(route.service_name);
    }
    if (dst != null) {
      dst.consumers += 1;
      // This isn't guaranteed, but assume that if there is a link from node A to B for a FIDL service
      // that node B offers that service.
      if (!dst.routes.offers.includes(route.service_name)) {
        dst.routes.offers.push(route.service_name);
      }
    }

    return link;
  }

  /**
   * Returns the complete json graph of all components.
   */
  async getGraph() {
    // Get all components and routes
    let components = await this.post_request(this.componentsUrl, null);
    let routes = await this.post_request(this.routesUrl, null);

    // Create an empty graph
    let graph = {
      links: [],
      nodes: [],
    };

    // Add all the components as nodes
    let nodes = {};
    let self = this;
    components.forEach(async function (component) {
      let [id, node] = self.makeNode(component);
      nodes[id] = node;
      graph.nodes.push(node);
    });
    this.nodes = nodes;

    // Fill out the links between nodes
    routes.forEach(async function (route) {
      let link = self.makeLink(route);
      graph.links.push(link);
    });

    return graph;
  }

  /**
   * Private internal API that returns a promise to the JSON response of a
   * GET request with no parameters.
   */
  get_request(url) {
    return new Promise(function(resolve, reject) {
      const jsonRequest= new XMLHttpRequest();
      jsonRequest.open("GET", url);
      // Only accept 200 success.
      jsonRequest.onload = function() {
        if (this.status == 200) {
          resolve(JSON.parse(jsonRequest.response));
        } else {
          reject({status: this.status, statusText: jsonRequest.statusText});
        }
      }
      // Reject all errors.
      jsonRequest.onerror = function() {
        reject({status: this.status, statusText: jsonRequest.statusText});
      }
      jsonRequest.send();
    });
  }

  /**
   * Private internal API that returns a promise to the JSON response of a 
   * POST request with an input request body.
   */
  post_request(url, body) {
    return new Promise(function(resolve, reject) {
      const jsonRequest= new XMLHttpRequest();
        jsonRequest.open("POST", url);
        // Only accept 200 success.
        jsonRequest.onload = function() {
          if (this.status == 200) {
            resolve(JSON.parse(jsonRequest.response));
          } else {
            reject({status: this.status, statusText: jsonRequest.statusText});
          }
        }
        // Reject all errors.
        jsonRequest.onerror = function() {
          reject({status: this.status, statusText: jsonRequest.statusText});
        }
        jsonRequest.send(body);
    });
  }
}

/**
 * Simple class that renders the view for the component mapper.
 */
class ComponentMapperView {
  constructor(api) {
    this.api = api;
    this.graphData = null;
    this.search = document.getElementById("cm-search");
    this.search.addEventListener("keyup", this.searchComponents);
    this.searchButton = document.getElementById("cm-search-button");
    this.searchButton.addEventListener("click", this.searchComponents);
    this.sidebar = document.getElementById("cm-component-list");
    this.map = document.getElementById("cm-map");
    this.logo = document.getElementById("cm-logo");

    document.body.onkeyup = (e) => {
      if (e.keyCode == 32) {
        this.resetGraphVisible();
      }
    }
    document.addEventListener('click', this.globalClickListener);
  }

  /**
   * Does the basic setup for the view and populates all the important elements.
   */
  async init() {
    // Retrieve all the important view state.
    await this.refresh();
    // Draw the sidebar.
    this.drawSidebar(this.graphData["nodes"]);
    // Draw the main graph.
    this.drawGraph();
  }

  /**
   * Refresh all the apis and retrieve new packages etc.
   */
  async refresh() {
    console.log("[ComponentManager] - Refresh Triggered");
    this.graphData = await this.api.getGraph();
  }

  /**
   * Draws the main graph.
   */
  drawGraph() {
    console.log("[ComponentView] - Drawing Graph");
    const map = document.getElementById("cm-map");
    const boundingRect = map.getBoundingClientRect();
    const width = boundingRect.width;
    const height = boundingRect.height;
    const viewBox = `0 0 ${width} ${height}`;
    const nodeRadius = 120;
    const nodeBounding = 2*nodeRadius;

    // Construct the map.
    const svg = d3.select("#cm-map")
        .append("svg")
          .attr("preserveAspectRatio", "xMinYMin meet")
          .attr("viewBox", viewBox)
          .attr("width", width)
          .attr("height", height)
          .call(d3.zoom().on("zoom", function() {
            svg.attr("transform", d3.event.transform)
          }))
        .append("g");

    const view = this;
    // Construct the graph.
    {
      let graph = view.graphData;
      
      console.log("[ComponentView] - Parsing Graph JSON");

      const link = svg.append("g").selectAll("line")
        .data(graph.links)
        .enter()
        .append("line")
          .style("stroke", function(e) {
            return "#64B5F6";
          });

      const node = svg.append("g").selectAll("circle")
        .data(graph.nodes)
        .enter()
        .append("circle")
          .attr("r", function(e) {
            let calculatedRadius = nodeRadius + 5*e.consumers + 5*e.routes.uses.length;
            if (e.type == "core") {
              calculatedRadius = calculatedRadius*2;
            }
            e.radius = calculatedRadius;
            return calculatedRadius;
          })
          .style("fill", defaultColorFill)
          .on("click", (e, i) => {
            const query = "id: " + e.id;
            view.search.value = query;
            view.updateSidebarWithSearchResults(query);
          });

      const label = svg.append("g").attr("class", "labels")
          .selectAll("text").data(graph.nodes).enter()
          .append("text")
          .attr("text-anchor","middle")
          .attr("class", "label")
          .style("font-size", function(e) {
            let calculatedSize = 20 + (e.consumers * 2) + (e.routes.uses.length * 2);
            if (e.type == "core") {
              calculatedSize = calculatedSize*2;
            }
            return calculatedSize;
          })
          .style("font-weight", "bold")
          .style("fill", "#e8eaed")
          .on("click", (e, i) => {
            const query = "id: " + e.id;
            view.search.value = query;
            view.updateSidebarWithSearchResults(query);
          })
          .text(function(e) { return e.name; });

      const simulate = d3.forceSimulation()
          .force("center", d3.forceCenter(width/2, height/2))
          .force("charge", d3.forceManyBody())
          .force("collide", d3.forceCollide().radius(function(e) { return 1.5* e.radius; }).iterations(1))
          .force("link", d3.forceLink().id(function(e) { return e.id; }));

      simulate
          .nodes(graph.nodes)
          .on("tick", function() {
            link
              .attr("x1", function(e) { return e.source.x; })
              .attr("y1", function(e) { return e.source.y; })
              .attr("x2", function(e) { return e.target.x; })
              .attr("y2", function(e) { return e.target.y; });

            node
              .attr("cx", function(e) { return e.x + 5 })
              .attr("cy", function(e) { return e.y - 3 });

            label
                .attr("x", function(e) { return e.x })
                .attr("y", function(e) { return e.y });
          })
          .on("end", function() {
            console.log("[ComponentView] Graph Stabilized");
          });
      simulate.force("link").links(graph.links);

      // Calculate what to do next.
    }
  }

  openRawManifest(component_id) {
    window.open(this.api.manifestUrl + "?component_id=" + component_id, "_blank");
  }

  globalClickListener = ele => {
    if (ele.target.className == "manifest-button" && ele.target.getAttribute("data-component-id") != null) {
      this.openRawManifest(ele.target.getAttribute("data-component-id"));
    }
  }

  /**
   * Draws the component sidebar with the listed items.
   */
  drawSidebar(graphNodes) {
    this.sidebar.innerHTML = "";
    // TODO(benwright) - Move these to use CSS margins.
    for (const graphNode of graphNodes) {
      const listNode = document.createElement("li");
      listNode.appendChild(document.createTextNode(graphNode["name"]));
      listNode.appendChild(document.createElement("br"));
      listNode.appendChild(document.createTextNode(graphNode["url"]));

      listNode.appendChild(document.createElement("br"));
      let hrefEle = document.createElement("button");
      hrefEle.className = "manifest-button";
      hrefEle.setAttribute("data-component-id", graphNode.id);
      hrefEle.appendChild(document.createTextNode("Manifest"));
      listNode.appendChild(hrefEle);
      if (graphNode["routes"]["uses"].length) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("uses:"));
        for (const use of graphNode["routes"]["uses"]) {
          listNode.appendChild(document.createElement("br"));
          listNode.appendChild(document.createTextNode(use));
        }
      }
      if (graphNode["routes"]["offers"].length) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("offers:"));
        for (const offer of graphNode["routes"]["offers"]) {
          listNode.appendChild(document.createElement("br"));
          listNode.appendChild(document.createTextNode(offer));
        }
      }
      listNode.appendChild(document.createElement("br"));
      listNode.appendChild(document.createElement("br"));
      listNode.appendChild(document.createTextNode("version: " + graphNode["version"]));
      if (graphNode["source"]) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("source: " + graphNode["source"]));
      }
      if (graphNode["type"]) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("type: " + graphNode["type"]));
      }
      if (graphNode["consumers"]) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("consumers: " + graphNode["consumers"]));
      }

      this.sidebar.appendChild(listNode);
    }
  }

  /**
   * Show a node and all nodes used by or using that node.
   */
  selectSingleNode(node, show_uses, show_used_by) {
    const componentName = node.name;
    const servicesOffered = node.routes.offers;
    const servicesUsed = node.routes.uses;
    d3.select('g').selectAll('circle').each(function(node) {
      if (node.name == componentName) {
        d3.select(this).attr("visibility", "visible").style("fill", searchColorFill("node"));
        return;
      }

      // Since we are iterating through the nodes to see if the node we have selected is used by 
      // the current loop node, the naming of the variables is a little confusing.
      // Essentially, show_used_by says that we only show the current loop node if it uses
      // a service provided by the selected node.
      if (show_used_by) {
        const uses = servicesOffered.some(e => node.routes.uses.indexOf(e) >= 0);
        if (uses) {
          d3.select(this).attr("visibility", "visible").style("fill", searchColorFill("uses"));
          return;
        }
      }

      if (show_uses) {
        const used_by = servicesUsed.some(e => node.routes.offers.indexOf(e) >= 0);
        if (used_by) {
          d3.select(this).attr("visibility", "visible").style("fill", searchColorFill("used_by"));
          return;
        }
      }

    });
    d3.select('g').selectAll('line').each(function(link) {
      if (link.source.name == componentName || link.target.name == componentName) {
        d3.select(this).attr("visibility", "visible");
      }
    });
  }

  /**
   * Resets the graph back to the state where all links and nodes are invisible.
   */
  resetGraphInvisible() {
    d3.select('g').selectAll('circle').each(function(node) {
      d3.select(this).attr("visibility", "hidden");
      d3.select(this).style("stroke", "").style("stroke-width", "").style("fill", defaultColorFill);
    });
    d3.select('g').selectAll('line').each(function(link) {
      d3.select(this).attr("visibility", "hidden");
    });
  }

  /**
   * Resets the graph back to the default state where all links and nodes are visible.
   */
  resetGraphVisible() {
    d3.select('g').selectAll('circle').each(function(node) {
      d3.select(this).attr("visibility", "visible");
      d3.select(this).style("stroke", "").style("stroke-width", "").style("fill", defaultColorFill);
    });
    d3.select('g').selectAll('line').each(function(link) {
      d3.select(this).attr("visibility", "visible");
    });
  }

  searchComponents = event => {
      // 13 is the enter key, only search whenever we either click on the search button or release the enter key.
      if (event.type == "click" || (event.type == "keyup" && event.keyCode == 13)) {
        this.updateSidebarWithSearchResults(this.search.value);
      }
  }

  updateSidebarWithSearchResults(query) {
    if (this.graphData == null) {
      return;
    }

    if (query == "") {
      this.resetGraphVisible();
      this.drawSidebar([]);
      return;
    }

    const matchingNodes = [];
    // Handle commands
    if (query.startsWith("uses: ")) {
      const subquery = query.split(" ")[1];
      for (const node of this.graphData["nodes"]) {
        for (const uses of node["routes"]["uses"]) {
          if (uses.match(subquery)) {
            matchingNodes.push(node);
            break;
          }
        }
      }
    } else if (query.startsWith("offers: ")) {
      const subquery = query.split(" ")[1];
      for (const node of this.graphData["nodes"]) {
        for (const offers of node["routes"]["offers"]) {
          if (offers.match(subquery)) {
            matchingNodes.push(node);
            break;
          }
        }
      }
    } else if (query.startsWith("id: ")) {
      const subquery = query.split(" ")[1];
      for (const node of this.graphData["nodes"]) {
        if (node["id"] == subquery) {
          // Since ids are unique, we can stop after finding a single node.
          matchingNodes.push(node);
          break;
        }
      }
    } else {
      for (const node of this.graphData["nodes"]) {
        if (node["name"].match(query)) {
          matchingNodes.push(node);
        }
      }
    }

    this.resetGraphInvisible();
    if (matchingNodes.length == 1) {
      d3.select('g').selectAll('circle').each(node => {
        if (node.name == matchingNodes[0].name) {
          this.selectSingleNode(node, true, true);
          return;
        }
      });
    } else if (matchingNodes.length > 1) {
      d3.select('g').selectAll('circle').each(node => {
        if (matchingNodes.some(e => e.name == node.name)) {
          this.selectSingleNode(node, false, false);
          return;
        }
      });
    } else {
      this.resetGraphVisible();
    }

    this.drawSidebar(matchingNodes);
  }
}

window.onload = async function(e) {
  const api = new ComponentMapperApi(location.origin + '/');
  const view = new ComponentMapperView(api);
  view.init();
}
