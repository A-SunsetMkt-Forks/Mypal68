/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const defer = require("devtools/shared/defer");
const {
  FrontClassWithSpec,
  types,
  registerFront,
} = require("devtools/shared/protocol.js");
const { walkerSpec } = require("devtools/shared/specs/walker");

loader.lazyRequireGetter(
  this,
  "nodeConstants",
  "devtools/shared/dom-node-constants"
);

/**
 * Client side of the DOM walker.
 */
class WalkerFront extends FrontClassWithSpec(walkerSpec) {
  /**
   * This is kept for backward-compatibility reasons with older remote target.
   * Targets previous to bug 916443
   */
  pick() {
    return super.pick().then(response => {
      return response.node;
    });
  }

  constructor(client, targetFront, parentFront) {
    super(client, targetFront, parentFront);
    this._createRootNodePromise();
    this._orphaned = new Set();
    this._retainedOrphans = new Set();

    // Set to true if cleanup should be requested after every mutation list.
    this.autoCleanup = true;

    this.before("new-mutations", this.onMutations.bind(this));
  }

  // Update the object given a form representation off the wire.
  form(json) {
    this.actorID = json.actor;
    this.rootNode = types.getType("domnode").read(json.root, this);
    this._rootNodeDeferred.resolve(this.rootNode);
    // FF42+ the actor starts exposing traits
    this.traits = json.traits || {};
  }

  /**
   * Clients can use walker.rootNode to get the current root node of the
   * walker, but during a reload the root node might be null.  This
   * method returns a promise that will resolve to the root node when it is
   * set.
   */
  getRootNode() {
    return this._rootNodeDeferred.promise;
  }

  /**
   * Create the root node promise, triggering the "new-root" notification
   * on resolution.
   */
  _createRootNodePromise() {
    this._rootNodeDeferred = defer();
    this._rootNodeDeferred.promise.then(() => {
      this.emit("new-root");
    });
  }

  /**
   * When reading an actor form off the wire, we want to hook it up to its
   * parent or host front.  The protocol guarantees that the parent will
   * be seen by the client in either a previous or the current request.
   * So if we've already seen this parent return it, otherwise create
   * a bare-bones stand-in node.  The stand-in node will be updated
   * with a real form by the end of the deserialization.
   */
  ensureDOMNodeFront(id) {
    const front = this.get(id);
    if (front) {
      return front;
    }

    return types.getType("domnode").read({ actor: id }, this, "standin");
  }

  /**
   * See the documentation for WalkerActor.prototype.retainNode for
   * information on retained nodes.
   *
   * From the client's perspective, `retainNode` can fail if the node in
   * question is removed from the ownership tree before the `retainNode`
   * request reaches the server.  This can only happen if the client has
   * asked the server to release nodes but hasn't gotten a response
   * yet: Either a `releaseNode` request or a `getMutations` with `cleanup`
   * set is outstanding.
   *
   * If either of those requests is outstanding AND releases the retained
   * node, this request will fail with noSuchActor, but the ownership tree
   * will stay in a consistent state.
   *
   * Because the protocol guarantees that requests will be processed and
   * responses received in the order they were sent, we get the right
   * semantics by setting our local retained flag on the node only AFTER
   * a SUCCESSFUL retainNode call.
   */
  retainNode(node) {
    return super.retainNode(node).then(() => {
      node.retained = true;
    });
  }

  unretainNode(node) {
    return super.unretainNode(node).then(() => {
      node.retained = false;
      if (this._retainedOrphans.has(node)) {
        this._retainedOrphans.delete(node);
        this._releaseFront(node);
      }
    });
  }

  releaseNode(node, options = {}) {
    // NodeFront.destroy will destroy children in the ownership tree too,
    // mimicking what the server will do here.
    const actorID = node.actorID;
    this._releaseFront(node, !!options.force);
    return super.releaseNode({ actorID: actorID });
  }

  findInspectingNode() {
    return super.findInspectingNode().then(response => {
      return response.node;
    });
  }

  querySelector(queryNode, selector) {
    return super.querySelector(queryNode, selector).then(response => {
      return response.node;
    });
  }

  async gripToNodeFront(grip) {
    const response = await this.getNodeActorFromObjectActor(grip.actor);
    const nodeFront = response ? response.node : null;
    if (!nodeFront) {
      throw new Error(
        "The ValueGrip passed could not be translated to a NodeFront"
      );
    }
    return nodeFront;
  }

  getNodeActorFromWindowID(windowID) {
    return super.getNodeActorFromWindowID(windowID).then(response => {
      return response ? response.node : null;
    });
  }

  getNodeActorFromContentDomReference(contentDomReference) {
    if (!this.traits.retrieveNodeFromContentDomReference) {
      console.error(
        "The server is too old to retrieve a node from a contentDomReference"
      );
      return Promise.resolve(null);
    }

    return super
      .getNodeActorFromContentDomReference(contentDomReference)
      .then(response => {
        return response ? response.node : null;
      });
  }

  getStyleSheetOwnerNode(styleSheetActorID) {
    return super.getStyleSheetOwnerNode(styleSheetActorID).then(response => {
      return response ? response.node : null;
    });
  }

  getNodeFromActor(actorID, path) {
    return super.getNodeFromActor(actorID, path).then(response => {
      return response ? response.node : null;
    });
  }

  /*
   * Incrementally search the document for a given string.
   * For modern servers, results will be searched with using the WalkerActor
   * `search` function (includes tag names, attributes, and text contents).
   * Only 1 result is sent back, and calling the method again with the same
   * query will send the next result. When there are no more results to be sent
   * back, null is sent.
   * @param {String} query
   * @param {Object} options
   *    - "reverse": search backwards
   */
  async search(query, options = {}) {
    const searchData = (this.searchData = this.searchData || {});
    const result = await super.search(query, options);
    const nodeList = result.list;

    // If this is a new search, start at the beginning.
    if (searchData.query !== query) {
      searchData.query = query;
      searchData.index = -1;
    }

    if (!nodeList.length) {
      return null;
    }

    // Move search result cursor and cycle if necessary.
    searchData.index = options.reverse
      ? searchData.index - 1
      : searchData.index + 1;
    if (searchData.index >= nodeList.length) {
      searchData.index = 0;
    }
    if (searchData.index < 0) {
      searchData.index = nodeList.length - 1;
    }

    // Send back the single node, along with any relevant search data
    const node = await nodeList.item(searchData.index);
    return {
      type: "search",
      node: node,
      resultsLength: nodeList.length,
      resultsIndex: searchData.index,
    };
  }

  _releaseFront(node, force) {
    if (node.retained && !force) {
      node.reparent(null);
      this._retainedOrphans.add(node);
      return;
    }

    if (node.retained) {
      // Forcing a removal.
      this._retainedOrphans.delete(node);
    }

    // Release any children
    for (const child of node.treeChildren()) {
      this._releaseFront(child, force);
    }

    // All children will have been removed from the node by this point.
    node.reparent(null);
    node.destroy();
  }

  /**
   * Get any unprocessed mutation records and process them.
   */
  getMutations(options = {}) {
    return super.getMutations(options).then(mutations => {
      const emitMutations = [];
      for (const change of mutations) {
        // The target is only an actorID, get the associated front.
        let targetID;
        let targetFront;

        if (change.type === "newRoot") {
          // We may receive a new root without receiving any documentUnload
          // beforehand. Like when opening tools in middle of a document load.
          if (this.rootNode) {
            this._createRootNodePromise();
          }
          this.rootNode = types.getType("domnode").read(change.target, this);
          this._rootNodeDeferred.resolve(this.rootNode);
          targetID = this.rootNode.actorID;
          targetFront = this.rootNode;
        } else {
          targetID = change.target;
          targetFront = this.get(targetID);
        }

        if (!targetFront) {
          console.warn(
            "Got a mutation for an unexpected actor: " +
              targetID +
              ", please file a bug on bugzilla.mozilla.org!"
          );
          console.trace();
          continue;
        }

        const emittedMutation = Object.assign(change, { target: targetFront });

        if (
          change.type === "childList" ||
          change.type === "nativeAnonymousChildList"
        ) {
          // Update the ownership tree according to the mutation record.
          const addedFronts = [];
          const removedFronts = [];
          for (const removed of change.removed) {
            const removedFront = this.get(removed);
            if (!removedFront) {
              console.error(
                "Got a removal of an actor we didn't know about: " + removed
              );
              continue;
            }
            // Remove from the ownership tree
            removedFront.reparent(null);

            // This node is orphaned unless we get it in the 'added' list
            // eventually.
            this._orphaned.add(removedFront);
            removedFronts.push(removedFront);
          }
          for (const added of change.added) {
            const addedFront = this.get(added);
            if (!addedFront) {
              console.error(
                "Got an addition of an actor we didn't know " +
                  "about: " +
                  added
              );
              continue;
            }
            addedFront.reparent(targetFront);

            // The actor is reconnected to the ownership tree, unorphan
            // it.
            this._orphaned.delete(addedFront);
            addedFronts.push(addedFront);
          }

          // Before passing to users, replace the added and removed actor
          // ids with front in the mutation record.
          emittedMutation.added = addedFronts;
          emittedMutation.removed = removedFronts;

          // If this is coming from a DOM mutation, the actor's numChildren
          // was passed in. Otherwise, it is simulated from a frame load or
          // unload, so don't change the front's form.
          if ("numChildren" in change) {
            targetFront._form.numChildren = change.numChildren;
          }
        } else if (change.type === "frameLoad") {
          // Nothing we need to do here, except verify that we don't have any
          // document children, because we should have gotten a documentUnload
          // first.
          for (const child of targetFront.treeChildren()) {
            if (child.nodeType === nodeConstants.DOCUMENT_NODE) {
              console.warn(
                "Got an unexpected frameLoad in the inspector, " +
                  "please file a bug on bugzilla.mozilla.org!"
              );
              console.trace();
            }
          }
        } else if (change.type === "documentUnload") {
          if (targetFront === this.rootNode) {
            this._createRootNodePromise();
          }

          // We try to give fronts instead of actorIDs, but these fronts need
          // to be destroyed now.
          emittedMutation.target = targetFront.actorID;
          emittedMutation.targetParent = targetFront.parentNode();

          // Release the document node and all of its children, even retained.
          this._releaseFront(targetFront, true);
        } else if (change.type === "shadowRootAttached") {
          targetFront._form.isShadowHost = true;
        } else if (change.type === "customElementDefined") {
          targetFront._form.customElementLocation =
            change.customElementLocation;
        } else if (change.type === "unretained") {
          // Retained orphans were force-released without the intervention of
          // client (probably a navigated frame).
          for (const released of change.nodes) {
            const releasedFront = this.get(released);
            this._retainedOrphans.delete(released);
            this._releaseFront(releasedFront, true);
          }
        } else {
          targetFront.updateMutation(change);
        }

        // Update the inlineTextChild property of the target for a selected list of
        // mutation types.
        if (
          change.type === "inlineTextChild" ||
          change.type === "childList" ||
          change.type === "shadowRootAttached" ||
          change.type === "nativeAnonymousChildList"
        ) {
          if (change.inlineTextChild) {
            targetFront.inlineTextChild = types
              .getType("domnode")
              .read(change.inlineTextChild, this);
          } else {
            targetFront.inlineTextChild = undefined;
          }
        }

        emitMutations.push(emittedMutation);
      }

      if (options.cleanup) {
        for (const node of this._orphaned) {
          // This will move retained nodes to this._retainedOrphans.
          this._releaseFront(node);
        }
        this._orphaned = new Set();
      }

      this.emit("mutations", emitMutations);
    });
  }

  /**
   * Handle the `new-mutations` notification by fetching the
   * available mutation records.
   */
  onMutations() {
    // Fetch and process the mutations.
    this.getMutations({ cleanup: this.autoCleanup }).catch(() => {});
  }

  isLocal() {
    return !!this.conn._transport._serverConnection;
  }

  async removeNode(node) {
    const previousSibling = await this.previousSibling(node);
    const nextSibling = await super.removeNode(node);
    return {
      previousSibling: previousSibling,
      nextSibling: nextSibling,
    };
  }

  /**
   * Evaluate the cross iframes query selectors for the current walker front.
   *
   * @param {Array} selectors
   *        An array of CSS selectors to find the target accessible object.
   *        Several selectors can be needed if the element is nested in frames
   *        and not directly in the root document.
   * @return {Promise} a promise that resolves when the node front is found for
   *                   selection using inspector tools.
   */
  async findNodeFront(nodeSelectors) {
    const querySelectors = async nodeFront => {
      const selector = nodeSelectors.shift();
      if (!selector) {
        return nodeFront;
      }
      nodeFront = await this.querySelector(nodeFront, selector);

      // It's possible the containing iframe isn't available by the time
      // this.querySelector is called, which causes the re-selected node to be
      // unavailable. There also isn't a way for us to know when all iframes on the page
      // have been created after a reload. Because of this, we should should bail here.
      if (!nodeFront) {
        return null;
      }

      if (nodeSelectors.length > 0) {
        if (nodeFront.traits.supportsWaitForFrameLoad) {
          // Backward compatibility: only FF72 or newer are able to wait for
          // iframes to load. After FF72 reaches release we can unconditionally
          // call waitForFrameLoad.
          await nodeFront.waitForFrameLoad();
        }

        const { nodes } = await this.children(nodeFront);

        // If there are remaining selectors to process, they will target a document or a
        // document-fragment under the current node. Whether the element is a frame or
        // a web component, it can only contain one document/document-fragment, so just
        // select the first one available.
        nodeFront = nodes.find(node => {
          const { nodeType } = node;
          return (
            nodeType === Node.DOCUMENT_FRAGMENT_NODE ||
            nodeType === Node.DOCUMENT_NODE
          );
        });
      }
      return querySelectors(nodeFront) || nodeFront;
    };
    const nodeFront = await this.getRootNode();

    // If rootSelectors are [frameSelector1, ..., frameSelectorN, rootSelector]
    // we expect that [frameSelector1, ..., frameSelectorN] will also be in
    // nodeSelectors.
    // Otherwise it means the nodeSelectors target a node outside of this walker
    // and we should return null.
    const rootFrontSelectors = await nodeFront.getAllSelectors();
    for (let i = 0; i < rootFrontSelectors.length - 1; i++) {
      if (rootFrontSelectors[i] !== nodeSelectors[i]) {
        return null;
      }
    }

    // The query will start from the walker's rootNode, remove all the
    // "frameSelectors".
    nodeSelectors.splice(0, rootFrontSelectors.length - 1);

    return querySelectors(nodeFront);
  }
}

exports.WalkerFront = WalkerFront;
registerFront(WalkerFront);
