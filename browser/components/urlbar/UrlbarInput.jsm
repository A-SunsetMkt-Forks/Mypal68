/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

var EXPORTED_SYMBOLS = ["UrlbarInput"];

const { XPCOMUtils } = ChromeUtils.import(
  "resource://gre/modules/XPCOMUtils.jsm"
);

XPCOMUtils.defineLazyModuleGetters(this, {
  AppConstants: "resource://gre/modules/AppConstants.jsm",
  BrowserUtils: "resource://gre/modules/BrowserUtils.jsm",
  ExtensionSearchHandler: "resource://gre/modules/ExtensionSearchHandler.jsm",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.jsm",
  ReaderMode: "resource://gre/modules/ReaderMode.jsm",
  Services: "resource://gre/modules/Services.jsm",
  UrlbarController: "resource:///modules/UrlbarController.jsm",
  UrlbarEventBufferer: "resource:///modules/UrlbarEventBufferer.jsm",
  UrlbarPrefs: "resource:///modules/UrlbarPrefs.jsm",
  UrlbarQueryContext: "resource:///modules/UrlbarUtils.jsm",
  UrlbarTokenizer: "resource:///modules/UrlbarTokenizer.jsm",
  UrlbarUtils: "resource:///modules/UrlbarUtils.jsm",
  UrlbarValueFormatter: "resource:///modules/UrlbarValueFormatter.jsm",
  UrlbarView: "resource:///modules/UrlbarView.jsm",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "ClipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  "nsIClipboardHelper"
);

let getBoundsWithoutFlushing = element =>
  element.ownerGlobal.windowUtils.getBoundsWithoutFlushing(element);
let px = number => number.toFixed(2) + "px";

/**
 * Implements the text input part of the address bar UI.
 */
class UrlbarInput {
  /**
   * @param {object} options
   *   The initial options for UrlbarInput.
   * @param {object} options.textbox
   *   The container element.
   */
  constructor(options = {}) {
    this.textbox = options.textbox;

    this.window = this.textbox.ownerGlobal;
    this.document = this.window.document;
    this.window.addEventListener("unload", this);

    // Create the panel to contain results.
    this.textbox.appendChild(
      this.window.MozXULElement.parseXULToFragment(`
        <vbox class="urlbarView"
              role="group"
              tooltip="aHTMLTooltip"
              context="">
          <html:div class="urlbarView-body-outer">
            <html:div class="urlbarView-body-inner">
              <html:div id="urlbar-results"
                        class="urlbarView-results"
                        role="listbox"/>
            </html:div>
          </html:div>
          <hbox class="search-one-offs"
                compact="true"
                includecurrentengine="true"
                disabletab="true"/>
        </vbox>
      `)
    );
    this.panel = this.textbox.querySelector(".urlbarView");

    // "Megabar" is the internal codename for the update1 design refresh.
    this.megabar = UrlbarPrefs.get("update1");
    if (this.megabar) {
      this.textbox.classList.add("megabar");
      this.textbox.parentNode.classList.add("megabar");
    }

    this.controller = new UrlbarController({
      browserWindow: this.window,
    });
    this.controller.setInput(this);
    this.view = new UrlbarView(this);
    this.valueIsTyped = false;
    this.isPrivate = PrivateBrowsingUtils.isWindowPrivate(this.window);
    this.lastQueryContextPromise = Promise.resolve();
    this._actionOverrideKeyCount = 0;
    this._autofillPlaceholder = "";
    this._lastSearchString = "";
    this._valueOnLastSearch = "";
    this._resultForCurrentValue = null;
    this._suppressStartQuery = false;
    this._suppressPrimaryAdjustment = false;
    this._untrimmedValue = "";

    // This exists only for tests.
    this._enableAutofillPlaceholder = true;

    // Forward certain methods and properties.
    const CONTAINER_METHODS = [
      "getAttribute",
      "hasAttribute",
      "querySelector",
      "setAttribute",
      "removeAttribute",
      "toggleAttribute",
    ];
    const INPUT_METHODS = [
      "addEventListener",
      "blur",
      "focus",
      "removeEventListener",
    ];
    const READ_WRITE_PROPERTIES = [
      "placeholder",
      "readOnly",
      "selectionStart",
      "selectionEnd",
    ];

    for (let method of CONTAINER_METHODS) {
      this[method] = (...args) => {
        return this.textbox[method](...args);
      };
    }

    for (let method of INPUT_METHODS) {
      this[method] = (...args) => {
        return this.inputField[method](...args);
      };
    }

    for (let property of READ_WRITE_PROPERTIES) {
      Object.defineProperty(this, property, {
        enumerable: true,
        get() {
          return this.inputField[property];
        },
        set(val) {
          return (this.inputField[property] = val);
        },
      });
    }

    this.inputField = this.querySelector("#urlbar-input");
    this.dropmarker = this.querySelector(".urlbar-history-dropmarker");
    this._inputContainer = this.querySelector("#urlbar-input-container");
    this._identityBox = this.querySelector("#identity-box");
    this._toolbar = this.textbox.closest("toolbar");

    XPCOMUtils.defineLazyGetter(this, "valueFormatter", () => {
      return new UrlbarValueFormatter(this);
    });

    // If the toolbar is not visible in this window or the urlbar is readonly,
    // we'll stop here, so that most properties of the input object are valid,
    // but we won't handle events.
    if (!this.window.toolbar.visible || this.readOnly) {
      return;
    }

    // The event bufferer can be used to defer events that may affect users
    // muscle memory; for example quickly pressing DOWN+ENTER should end up
    // on a predictable result, regardless of the search status. The event
    // bufferer will invoke the handling code at the right time.
    this.eventBufferer = new UrlbarEventBufferer(this);

    this._inputFieldEvents = [
      "compositionstart",
      "compositionend",
      "contextmenu",
      "dragover",
      "dragstart",
      "drop",
      "focus",
      "blur",
      "input",
      "keydown",
      "keyup",
      "mouseover",
      "overflow",
      "underflow",
      "paste",
      "scrollend",
      "select",
    ];
    for (let name of this._inputFieldEvents) {
      this.addEventListener(name, this);
    }

    this.dropmarker.addEventListener("mousedown", this);
    this.window.addEventListener("mousedown", this);
    this.textbox.addEventListener("mousedown", this);
    this._inputContainer.addEventListener("click", this);

    this.window.gBrowser.tabContainer.addEventListener("TabSelect", this);

    this.window.addEventListener("customizationstarting", this);
    this.window.addEventListener("aftercustomization", this);

    this.updateLayoutBreakout();

    this._initCopyCutController();
    this._initPasteAndGo();

    // Tracks IME composition.
    this._compositionState = UrlbarUtils.COMPOSITION.NONE;
    this._compositionClosedPopup = false;

    this.editor.newlineHandling =
      Ci.nsIEditor.eNewlinesStripSurroundingWhitespace;

    this._setOpenViewOnFocus();
    Services.prefs.addObserver("browser.urlbar.openViewOnFocus", this);
  }

  /**
   * Shortens the given value, usually by removing http:// and trailing slashes,
   * such that calling nsIURIFixup::createFixupURI with the result will produce
   * the same URI.
   *
   * @param {string} val
   *   The string to be trimmed if it appears to be URI
   * @returns {string}
   *   The trimmed string
   */
  trimValue(val) {
    return UrlbarPrefs.get("trimURLs") ? BrowserUtils.trimURL(val) : val;
  }

  /**
   * Applies styling to the text in the urlbar input, depending on the text.
   */
  formatValue() {
    // The editor may not exist if the toolbar is not visible.
    if (this.editor) {
      this.valueFormatter.update();
    }
  }

  select() {
    // See _on_select().  HTMLInputElement.select() dispatches a "select"
    // event but does not set the primary selection.
    this._suppressPrimaryAdjustment = true;
    this.inputField.select();
    this._suppressPrimaryAdjustment = false;
  }

  /**
   * Converts an internal URI (e.g. a URI with a username or password) into one
   * which we can expose to the user.
   *
   * @param {nsIURI} uri
   *   The URI to be converted
   * @returns {nsIURI}
   *   The converted, exposable URI
   */
  makeURIReadable(uri) {
    // Avoid copying 'about:reader?url=', and always provide the original URI:
    // Reader mode ensures we call createExposableURI itself.
    let readerStrippedURI = ReaderMode.getOriginalUrlObjectForDisplay(
      uri.displaySpec
    );
    if (readerStrippedURI) {
      return readerStrippedURI;
    }

    try {
      return Services.io.createExposableURI(uri);
    } catch (ex) {}

    return uri;
  }

  observe(subject, topic, data) {
    switch (data) {
      case "browser.urlbar.openViewOnFocus":
        this._setOpenViewOnFocus();
        break;
    }
  }

  /**
   * Passes DOM events to the _on_<event type> methods.
   * @param {Event} event
   */
  handleEvent(event) {
    let methodName = "_on_" + event.type;
    if (methodName in this) {
      this[methodName](event);
    } else {
      throw new Error("Unrecognized UrlbarInput event: " + event.type);
    }
  }

  /**
   * Handles an event which would cause a url or text to be opened.
   *
   * @param {Event} event The event triggering the open.
   * @param {string} [openWhere] Where we expect the result to be opened.
   * @param {object} [openParams]
   *   The parameters related to where the result will be opened.
   * @param {object} [triggeringPrincipal]
   *   The principal that the action was triggered from.
   */
  handleCommand(event, openWhere, openParams = {}, triggeringPrincipal = null) {
    let isMouseEvent = event instanceof this.window.MouseEvent;
    if (isMouseEvent && event.button == 2) {
      // Do nothing for right clicks.
      return;
    }

    // Determine whether to use the selected one-off search button.  In
    // one-off search buttons parlance, "selected" means that the button
    // has been navigated to via the keyboard.  So we want to use it if
    // the triggering event is not a mouse click -- i.e., it's a Return
    // key -- or if the one-off was mouse-clicked.
    let selectedOneOff;
    if (this.view.isOpen) {
      selectedOneOff = this.view.oneOffSearchButtons.selectedButton;
      if (selectedOneOff && isMouseEvent && event.target != selectedOneOff) {
        selectedOneOff = null;
      }
      // Do the command of the selected one-off if it's not an engine.
      if (selectedOneOff && !selectedOneOff.engine) {
        selectedOneOff.doCommand();
        return;
      }
    }

    // Use the selected result if we have one; this is usually the case
    // when the view is open.
    let result = this.view.selectedResult;
    if (!selectedOneOff && result) {
      this.pickResult(result, event);
      return;
    }

    let url;
    if (selectedOneOff) {
      // If there's a selected one-off button then load a search using
      // the button's engine.
      result = this._resultForCurrentValue;
      let searchString =
        (result && (result.payload.suggestion || result.payload.query)) ||
        this._lastSearchString;
      [url, openParams.postData] = UrlbarUtils.getSearchQueryUrl(
        selectedOneOff.engine,
        searchString
      );
    } else {
      // Use the current value if we don't have a UrlbarResult e.g. because the
      // view is closed.
      url = this.untrimmedValue;
      openParams.postData = null;
    }

    if (!url) {
      return;
    }

    let where = openWhere || this._whereToOpen(event);
    openParams.allowInheritPrincipal = false;
    url = this._maybeCanonizeURL(event, url) || url.trim();

    try {
      new URL(url);
    } catch (ex) {
      let browser = this.window.gBrowser.selectedBrowser;
      let lastLocationChange = browser.lastLocationChange;

      UrlbarUtils.getShortcutOrURIAndPostData(url).then(data => {
        if (
          where != "current" ||
          browser.lastLocationChange == lastLocationChange
        ) {
          openParams.postData = data.postData;
          openParams.allowInheritPrincipal = data.mayInheritPrincipal;
          this._loadURL(data.url, where, openParams, null, browser);
        }
      });
      return;
    }

    this._loadURL(url, where, openParams);
  }

  handleRevert() {
    this.window.gBrowser.userTypedValue = null;
    this.window.URLBarSetURI(null, true);
    if (this.value && this.focused) {
      this.select();
    }
  }

  /**
   * Called by the view when a result is picked.
   *
   * @param {UrlbarResult} result The result that was picked.
   * @param {Event} event The event that picked the result.
   */
  pickResult(result, event) {
    let originalUntrimmedValue = this.value;
    let isCanonized = this.setValueFromResult(result, event);
    let where = this._whereToOpen(event);
    let openParams = {
      allowInheritPrincipal: false,
    };

    if (!result.payload.keywordOffer) {
      this.view.close();
    }

    if (isCanonized) {
      this._loadURL(this.value, where, openParams);
      return;
    }

    let { url, postData } = UrlbarUtils.getUrlFromResult(result);
    openParams.postData = postData;

    switch (result.type) {
      case UrlbarUtils.RESULT_TYPE.URL: {
        // Bug 1578856: both the provider and the docshell run heuristics to
        // decide how to handle a non-url string, either fixing it to a url, or
        // searching for it.
        // Some preferences can control the docshell behavior, for example
        // if dns_first_for_single_words is true, the docshell looks up the word
        // against the dns server, and either loads it as an url or searches for
        // it, depending on the lookup result. The provider instead will always
        // return a fixed url in this case, because URIFixup is synchronous and
        // can't do a synchronous dns lookup. A possible long term solution
        // would involve sharing the docshell logic with the provider, along
        // with the dns lookup.
        // For now, in this specific case, we'll override the result's url
        // with the input value, and let it pass through to _loadURL(), and
        // finally to the docshell.
        // This also means that in some cases the heuristic result will show a
        // Visit entry, but the docshell will instead execute a search. It's a
        // rare case anyway, most likely to happen for enterprises customizing
        // the urifixup prefs.
        if (
          result.heuristic &&
          UrlbarPrefs.get("browser.fixup.dns_first_for_single_words") &&
          UrlbarUtils.looksLikeSingleWordHost(originalUntrimmedValue)
        ) {
          url = originalUntrimmedValue;
        }
        break;
      }
      case UrlbarUtils.RESULT_TYPE.KEYWORD: {
        // If this result comes from a bookmark keyword, let it inherit the
        // current document's principal, otherwise bookmarklets would break.
        openParams.allowInheritPrincipal = true;
        break;
      }
      case UrlbarUtils.RESULT_TYPE.TAB_SWITCH: {
        if (this.hasAttribute("actionoverride")) {
          where = "current";
          break;
        }

        this.handleRevert();
        let prevTab = this.window.gBrowser.selectedTab;
        let loadOpts = {
          adoptIntoActiveWindow: UrlbarPrefs.get(
            "switchTabs.adoptIntoActiveWindow"
          ),
        };

        if (
          this.window.switchToTabHavingURI(
            Services.io.newURI(url),
            false,
            loadOpts
          ) &&
          prevTab.isEmpty
        ) {
          this.window.gBrowser.removeTab(prevTab);
        }
        return;
      }
      case UrlbarUtils.RESULT_TYPE.SEARCH: {
        if (result.payload.keywordOffer) {
          // The user confirmed a token alias, so just move the caret
          // to the end of it. Because there's a trailing space in the value,
          // the user can directly start typing a query string at that point.
          this.selectionStart = this.selectionEnd = this.value.length;

          // Picking a keyword offer just fills it in the input and doesn't
          // visit anything.  The user can then type a search string.  Also
          // start a new search so that the offer appears in the view by itself
          // to make it even clearer to the user what's going on.
          this.startQuery();
          return;
        }

        if (
          result.heuristic &&
          UrlbarUtils.looksLikeSingleWordHost(originalUntrimmedValue)
        ) {
          // The docshell when fixes a single word to a search, also checks the
          // dns and prompts the user whether they wanted to rather visit that
          // as a host. On a positive answer, it adds to the domains whitelist
          // that we use to make decisions. Because here we are directly asking
          // for a search, bypassing the docshell, we must do it here.
          // See URIFixupChild.jsm and keyword-uri-fixup.
          let flags =
            Ci.nsIURIFixup.FIXUP_FLAG_FIX_SCHEME_TYPOS |
            Ci.nsIURIFixup.FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
          // Don't interrupt the load action in case of errors.
          try {
            let fixupInfo = Services.uriFixup.getFixupURIInfo(
              originalUntrimmedValue.trim(),
              flags
            );
            this.window.gKeywordURIFixup({
              target: this.window.gBrowser.selectedBrowser,
              data: fixupInfo,
            });
          } catch (ex) {
            Cu.reportError(
              `An error occured while trying to fixup "${originalUntrimmedValue.trim()}": ${ex}`
            );
          }
        }

        const actionDetails = {
          isSuggestion: !!result.payload.suggestion,
          alias: result.payload.keyword,
        };
        const engine = Services.search.getEngineByName(result.payload.engine);
        break;
      }
      case UrlbarUtils.RESULT_TYPE.OMNIBOX: {
        // The urlbar needs to revert to the loaded url when a command is
        // handled by the extension.
        this.handleRevert();
        // We don't directly handle a load when an Omnibox API result is picked,
        // instead we forward the request to the WebExtension itself, because
        // the value may not even be a url.
        // We pass the keyword and content, that actually is the retrieved value
        // prefixed by the keyword. ExtensionSearchHandler uses this keyword
        // redundancy as a sanity check.
        ExtensionSearchHandler.handleInputEntered(
          result.payload.keyword,
          result.payload.content,
          where
        );
        return;
      }
    }

    if (!url) {
      throw new Error(`Invalid url for result ${JSON.stringify(result)}`);
    }

    if (!this.isPrivate && !result.heuristic) {
      // This should not interrupt the load anyway.
      UrlbarUtils.addToInputHistory(url, this._lastSearchString).catch(
        Cu.reportError
      );
    }

    this._loadURL(url, where, openParams, {
      source: result.source,
      type: result.type,
    });
  }

  /**
   * Called by the view when moving through results with the keyboard, and when
   * picking a result.
   *
   * @param {UrlbarResult} [result]
   *   The result that was selected or picked, null if no result was selected.
   * @param {Event} [event] The event that picked the result.
   * @returns {boolean}
   *   Whether the value has been canonized
   */
  setValueFromResult(result = null, event = null) {
    let canonizedUrl;

    if (!result) {
      // This usually happens when there's no selected results (the user cycles
      // through results and there was no heuristic), and we reset the input
      // value to the previous text value.
      this.value = this._valueOnLastSearch;
    } else {
      // For autofilled results, the value that should be canonized is not the
      // autofilled value but the value that the user typed.
      canonizedUrl = this._maybeCanonizeURL(
        event,
        result.autofill ? this._lastSearchString : this.value
      );
      if (canonizedUrl) {
        this.value = canonizedUrl;
      } else if (result.autofill) {
        let { value, selectionStart, selectionEnd } = result.autofill;
        this._autofillValue(value, selectionStart, selectionEnd);
      } else {
        this.value = this._getValueFromResult(result);
      }
    }
    this._resultForCurrentValue = result;

    // The value setter clobbers the actiontype attribute, so update this after
    // that.
    if (result) {
      switch (result.type) {
        case UrlbarUtils.RESULT_TYPE.TAB_SWITCH:
          this.setAttribute("actiontype", "switchtab");
          break;
        case UrlbarUtils.RESULT_TYPE.OMNIBOX:
          this.setAttribute("actiontype", "extension");
          break;
      }
    }

    return !!canonizedUrl;
  }

  /**
   * Called by the controller when the first result of a new search is received.
   * If it's an autofill result, then it may need to be autofilled, subject to a
   * few restrictions.
   *
   * @param {UrlbarResult} result
   *   The first result.
   */
  autofillFirstResult(result) {
    if (!result.autofill) {
      return;
    }

    let isPlaceholderSelected =
      this.selectionEnd == this._autofillPlaceholder.length &&
      this.selectionStart == this._lastSearchString.length &&
      this._autofillPlaceholder
        .toLocaleLowerCase()
        .startsWith(this._lastSearchString.toLocaleLowerCase());

    // Don't autofill if there's already a selection (with one caveat described
    // next) or the cursor isn't at the end of the input.  But if there is a
    // selection and it's the autofill placeholder value, then do autofill.
    if (
      !isPlaceholderSelected &&
      !this._autofillIgnoresSelection &&
      (this.selectionStart != this.selectionEnd ||
        this.selectionEnd != this._lastSearchString.length)
    ) {
      return;
    }

    this.setValueFromResult(result);
  }

  /**
   * Invoked by the view when the first result is received.
   * To prevent selection flickering, we apply autofill on input through a
   * placeholder, without waiting for results.
   * But, if the first result is not an autofill one, the autofill prediction
   * was wrong and we should restore the original user typed string.
   * @param {UrlbarResult} firstResult The first result received.
   */
  maybeClearAutofillPlaceholder(firstResult) {
    if (
      this._autofillPlaceholder &&
      !firstResult.autofill &&
      // Avoid clobbering added spaces (for token aliases, for example).
      !this.value.endsWith(" ")
    ) {
      this._setValue(this.window.gBrowser.userTypedValue, false);
    }
  }

  /**
   * Starts a query based on the current input value.
   *
   * @param {boolean} [options.allowAutofill]
   *   Whether or not to allow providers to include autofill results.
   * @param {boolean} [options.autofillIgnoresSelection]
   *   Normally we autofill only if the cursor is at the end of the string,
   *   if this is set we'll autofill regardless of selection.
   * @param {string} [options.searchString]
   *   The search string.  If not given, the current input value is used.
   *   Otherwise, the current input value must start with this value.
   * @param {boolean} [options.resetSearchState]
   *   If this is the first search of a user interaction with the input, set
   *   this to true (the default) so that search-related state from the previous
   *   interaction doesn't interfere with the new interaction.  Otherwise set it
   *   to false so that state is maintained during a single interaction.  The
   *   intended use for this parameter is that it should be set to false when
   *   this method is called due to input events.
   */
  startQuery({
    allowAutofill = true,
    autofillIgnoresSelection = false,
    searchString = null,
    resetSearchState = true,
  } = {}) {
    if (this._suppressStartQuery) {
      return;
    }

    this._autofillIgnoresSelection = autofillIgnoresSelection;
    if (resetSearchState) {
      this._resetSearchState();
    }

    if (!searchString) {
      searchString =
        this.getAttribute("pageproxystate") == "valid" ? "" : this.value;
    } else if (!this.value.startsWith(searchString)) {
      throw new Error("The current value doesn't start with the search string");
    }

    this._lastSearchString = searchString;
    this._valueOnLastSearch = this.value;

    // TODO (Bug 1522902): This promise is necessary for tests, because some
    // tests are not listening for completion when starting a query through
    // other methods than startQuery (input events for example).
    this.lastQueryContextPromise = this.controller.startQuery(
      new UrlbarQueryContext({
        allowAutofill,
        isPrivate: this.isPrivate,
        maxResults: UrlbarPrefs.get("maxRichResults"),
        muxer: "UnifiedComplete",
        searchString,
        userContextId: this.window.gBrowser.selectedBrowser.getAttribute(
          "usercontextid"
        ),
      })
    );
  }

  /**
   * Sets the input's value, starts a search, and opens the view.
   *
   * @param {string} value
   *   The input's value will be set to this value, and the search will
   *   use it as its query.
   */
  search(value) {
    this.window.focusAndSelectUrlBar();

    // If the value is a restricted token, append a space.
    if (Object.values(UrlbarTokenizer.RESTRICT).includes(value)) {
      this.inputField.value = value + " ";
    } else {
      this.inputField.value = value;
    }

    // Avoid selecting the text if this method is called twice in a row.
    this.selectionStart = -1;

    // Note: proper IME Composition handling depends on the fact this generates
    // an input event, rather than directly invoking the controller; everything
    // goes through _on_input, that will properly skip the search until the
    // composition is committed. _on_input also skips the search when it's the
    // same as the previous search, but we want to allow consecutive searches
    // with the same string. So clear _lastSearchString first.
    this._lastSearchString = "";
    let event = this.document.createEvent("UIEvents");
    event.initUIEvent("input", true, false, this.window, 0);
    this.inputField.dispatchEvent(event);
  }

  /**
   * Focus without the focus styles.
   * This is used by Activity Stream and about:privatebrowsing for search hand-off.
   */
  setHiddenFocus() {
    this._hideFocus = true;
    if (this.focused) {
      this.removeAttribute("focused");
    } else {
      this.focus();
    }
  }

  /**
   * Restore focus styles.
   * This is used by Activity Stream and about:privatebrowsing for search hand-off.
   */
  removeHiddenFocus() {
    this._hideFocus = false;
    if (this.focused) {
      this.setAttribute("focused", "true");
      this.startLayoutExtend();
    }
  }

  // Getters and Setters below.

  get editor() {
    return this.inputField.editor;
  }

  get focused() {
    return this.document.activeElement == this.inputField;
  }

  get goButton() {
    return this.querySelector("#urlbar-go-button");
  }

  get value() {
    return this.inputField.value;
  }

  get untrimmedValue() {
    return this._untrimmedValue;
  }

  set value(val) {
    return this._setValue(val, true);
  }

  async updateLayoutBreakout() {
    if (!this.megabar) {
      return;
    }
    if (!this._toolbar) {
      // Expanding requires a parent toolbar.
      return;
    }
    await this._updateLayoutBreakoutDimensions();
    this.startLayoutExtend();
  }

  startLayoutExtend() {
    // Do not expand if:
    // The Urlbar does not support being expanded or it is already expanded
    if (
      !this.hasAttribute("breakout") ||
      this.hasAttribute("breakout-extend")
    ) {
      return;
    }
    // The Urlbar is unfocused and the view is closed
    if (this.getAttribute("focused") != "true" && !this.view.isOpen) {
      return;
    }

    if (UrlbarPrefs.get("disableExtendForTests")) {
      this.setAttribute("breakout-extend-disabled", "true");
      return;
    }
    this.removeAttribute("breakout-extend-disabled");

    this._toolbar.setAttribute("urlbar-exceeds-toolbar-bounds", "true");
    this.setAttribute("breakout-extend", "true");
  }

  endLayoutExtend() {
    if (
      !this.hasAttribute("breakout-extend") ||
      this.view.isOpen ||
      this.getAttribute("focused") == "true"
    ) {
      return;
    }
    this.removeAttribute("breakout-extend");
    this._toolbar.removeAttribute("urlbar-exceeds-toolbar-bounds");
  }

  setPageProxyState(state) {
    this.setAttribute("pageproxystate", state);
    this._inputContainer.setAttribute("pageproxystate", state);
    this._identityBox.setAttribute("pageproxystate", state);
  }

  /**
   * When switching tabs quickly, TabSelect sometimes happens before
   * _adjustFocusAfterTabSwitch and due to the focus still being on the old
   * tab, we end up flickering the results pane briefly.
   */
  afterTabSwitchFocusChange() {
    this._gotFocusChange = true;
    this._afterTabSelectAndFocusChange();
  }

  // Private methods below.

  _afterTabSelectAndFocusChange() {
    // We must have seen both events to proceed safely.
    if (!this._gotFocusChange || !this._gotTabSelect) {
      return;
    }
    this._gotFocusChange = this._gotTabSelect = false;

    this._resetSearchState();

    // Switching tabs doesn't always change urlbar focus, so we must try to
    // reopen here too, not just on focus.
    // We don't use the original TabSelect event because caching it causes
    // leaks on MacOS.
    if (this.view.autoOpen({ event: new CustomEvent("tabswitch") })) {
      return;
    }
    // The input may retain focus when switching tabs in which case we
    // need to close the view explicitly.
    this.view.close();
  }

  async _updateLayoutBreakoutDimensions() {
    // When this method gets called a second time before the first call
    // finishes, we need to disregard the first one.
    let updateKey = {};
    this._layoutBreakoutUpdateKey = updateKey;

    this.removeAttribute("breakout");
    this.textbox.parentNode.removeAttribute("breakout");

    await this.window.promiseDocumentFlushed(() => {});
    await new Promise(resolve => {
      this.window.requestAnimationFrame(() => {
        if (this._layoutBreakoutUpdateKey != updateKey) {
          return;
        }

        this.textbox.parentNode.style.setProperty(
          "--urlbar-container-height",
          px(getBoundsWithoutFlushing(this.textbox.parentNode).height)
        );
        this.textbox.style.setProperty(
          "--urlbar-height",
          px(getBoundsWithoutFlushing(this.textbox).height)
        );
        this.textbox.style.setProperty(
          "--urlbar-toolbar-height",
          px(getBoundsWithoutFlushing(this._toolbar).height)
        );

        this.setAttribute("breakout", "true");
        this.textbox.parentNode.setAttribute("breakout", "true");

        resolve();
      });
    });
  }

  _setOpenViewOnFocus() {
    // FIXME: Not using UrlbarPrefs because its pref observer may run after
    // this call, so we'd get the previous openViewOnFocus value here. This
    // can be cleaned up after bug 1560013.
    this.openViewOnFocus = Services.prefs.getBoolPref(
      "browser.urlbar.openViewOnFocus"
    );
    this.dropmarker.hidden = this.openViewOnFocus;
  }

  _setValue(val, allowTrim) {
    this._untrimmedValue = val;

    let originalUrl = ReaderMode.getOriginalUrlObjectForDisplay(val);
    if (originalUrl) {
      val = originalUrl.displaySpec;
    }

    val = allowTrim ? this.trimValue(val) : val;

    this.valueIsTyped = false;
    this._resultForCurrentValue = null;
    this.inputField.value = val;
    this.formatValue();
    this.removeAttribute("actiontype");

    // Dispatch ValueChange event for accessibility.
    let event = this.document.createEvent("Events");
    event.initEvent("ValueChange", true, true);
    this.inputField.dispatchEvent(event);

    return val;
  }

  _getValueFromResult(result) {
    switch (result.type) {
      case UrlbarUtils.RESULT_TYPE.KEYWORD:
        return result.payload.input;
      case UrlbarUtils.RESULT_TYPE.SEARCH:
        return (
          (result.payload.keyword ? result.payload.keyword + " " : "") +
          (result.payload.suggestion || result.payload.query)
        );
      case UrlbarUtils.RESULT_TYPE.OMNIBOX:
        return result.payload.content;
    }

    try {
      let uri = Services.io.newURI(result.payload.url);
      if (uri) {
        return this.window.losslessDecodeURI(uri);
      }
    } catch (ex) {}

    return "";
  }

  /**
   * Resets some state so that searches from the user's previous interaction
   * with the input don't interfere with searches from a new interaction.
   */
  _resetSearchState() {
    this._lastSearchString = this.value;
    this._autofillPlaceholder = "";
  }

  /**
   * Autofills the autofill placeholder string if appropriate, and determines
   * whether autofill should be allowed for the new search started by an input
   * event.
   *
   * @param {string} value
   *   The new search string.
   * @returns {boolean}
   *   Whether autofill should be allowed in the new search.
   */
  _maybeAutofillOnInput(value) {
    let allowAutofill = this.selectionEnd == value.length;

    // Determine whether we can autofill the placeholder.  The placeholder is a
    // value that we autofill now, when the search starts and before we wait on
    // its first result, in order to prevent a flicker in the input caused by
    // the previous autofilled substring disappearing and reappearing when the
    // first result arrives.  Of course we can only autofill the placeholder if
    // it starts with the new search string, and we shouldn't autofill anything
    // if the caret isn't at the end of the input.
    if (
      !allowAutofill ||
      this._autofillPlaceholder.length <= value.length ||
      !this._autofillPlaceholder
        .toLocaleLowerCase()
        .startsWith(value.toLocaleLowerCase())
    ) {
      this._autofillPlaceholder = "";
    } else if (
      this._autofillPlaceholder &&
      this.selectionEnd == this.value.length &&
      this._enableAutofillPlaceholder
    ) {
      let autofillValue =
        value + this._autofillPlaceholder.substring(value.length);
      this._autofillValue(autofillValue, value.length, autofillValue.length);
    }

    return allowAutofill;
  }

  _updateTextOverflow() {
    if (!this._overflowing) {
      this.removeAttribute("textoverflow");
      return;
    }

    this.window.promiseDocumentFlushed(() => {
      // Check overflow again to ensure it didn't change in the meantime.
      let input = this.inputField;
      if (input && this._overflowing) {
        let side =
          input.scrollLeft && input.scrollLeft == input.scrollLeftMax
            ? "start"
            : "end";
        this.window.requestAnimationFrame(() => {
          // And check once again, since we might have stopped overflowing
          // since the promiseDocumentFlushed callback fired.
          if (this._overflowing) {
            this.setAttribute("textoverflow", side);
          }
        });
      }
    });
  }

  _updateUrlTooltip() {
    if (this.focused || !this._overflowing) {
      this.inputField.removeAttribute("title");
    } else {
      this.inputField.setAttribute("title", this.untrimmedValue);
    }
  }

  _getSelectedValueForClipboard() {
    let selection = this.editor.selection;
    const flags =
      Ci.nsIDocumentEncoder.OutputPreformatted |
      Ci.nsIDocumentEncoder.OutputRaw;
    let selectedVal = selection.toStringWithFormat("text/plain", flags, 0);

    // Handle multiple-range selection as a string for simplicity.
    if (selection.rangeCount > 1) {
      return selectedVal;
    }

    // If the selection doesn't start at the beginning or doesn't span the
    // full domain or the URL bar is modified or there is no text at all,
    // nothing else to do here.
    if (this.selectionStart > 0 || this.valueIsTyped || selectedVal == "") {
      return selectedVal;
    }

    // The selection doesn't span the full domain if it doesn't contain a slash and is
    // followed by some character other than a slash.
    if (!selectedVal.includes("/")) {
      let remainder = this.value.replace(selectedVal, "");
      if (remainder != "" && remainder[0] != "/") {
        return selectedVal;
      }
    }

    let uri;
    if (this.getAttribute("pageproxystate") == "valid") {
      uri = this.window.gBrowser.currentURI;
    } else {
      // The value could be:
      // 1. a trimmed url, set by selecting a result
      // 2. a search string set by selecting a result
      // 3. a url that was confirmed but didn't finish loading yet
      // If it's an url the untrimmedValue should resolve to a valid URI,
      // otherwise it's a search string that should be copied as-is.
      try {
        uri = Services.io.newURI(this._untrimmedValue);
      } catch (ex) {
        return selectedVal;
      }
    }
    uri = this.makeURIReadable(uri);

    // If the entire URL is selected, just use the actual loaded URI,
    // unless we want a decoded URI, or it's a data: or javascript: URI,
    // since those are hard to read when encoded.
    if (
      this.value == selectedVal &&
      !uri.schemeIs("javascript") &&
      !uri.schemeIs("data") &&
      !UrlbarPrefs.get("decodeURLsOnCopy")
    ) {
      return uri.displaySpec;
    }

    // Just the beginning of the URL is selected, or we want a decoded
    // url. First check for a trimmed value.
    let spec = uri.displaySpec;
    let trimmedSpec = this.trimValue(spec);
    if (spec != trimmedSpec) {
      // Prepend the portion that trimValue removed from the beginning.
      // This assumes trimValue will only truncate the URL at
      // the beginning or end (or both).
      let trimmedSegments = spec.split(trimmedSpec);
      selectedVal = trimmedSegments[0] + selectedVal;
    }

    return selectedVal;
  }

  _toggleActionOverride(event) {
    // Ignore repeated KeyboardEvents.
    if (event.repeat) {
      return;
    }
    if (
      event.keyCode == KeyEvent.DOM_VK_SHIFT ||
      event.keyCode == KeyEvent.DOM_VK_ALT ||
      event.keyCode ==
        (AppConstants.platform == "macosx"
          ? KeyEvent.DOM_VK_META
          : KeyEvent.DOM_VK_CONTROL)
    ) {
      if (event.type == "keydown") {
        this._actionOverrideKeyCount++;
        this.setAttribute("actionoverride", "true");
        this.view.panel.setAttribute("actionoverride", "true");
      } else if (
        this._actionOverrideKeyCount &&
        --this._actionOverrideKeyCount == 0
      ) {
        this._clearActionOverride();
      }
    }
  }

  _clearActionOverride() {
    this._actionOverrideKeyCount = 0;
    this.removeAttribute("actionoverride");
    this.view.panel.removeAttribute("actionoverride");
  }

  /**
   * If appropriate, this prefixes a search string with 'www.' and suffixes it
   * with browser.fixup.alternate.suffix prior to navigating.
   *
   * @param {Event} event
   *   The event that triggered this query.
   * @param {string} value
   *   The search string that should be canonized.
   * @returns {string}
   *   Returns the canonized URL if available and null otherwise.
   */
  _maybeCanonizeURL(event, value) {
    // Only add the suffix when the URL bar value isn't already "URL-like",
    // and only if we get a keyboard event, to match user expectations.
    if (
      !(event instanceof KeyboardEvent) ||
      !event.ctrlKey ||
      !UrlbarPrefs.get("ctrlCanonizesURLs") ||
      !/^\s*[^.:\/\s]+(?:\/.*|\s*)$/i.test(value)
    ) {
      return null;
    }

    let suffix = Services.prefs.getCharPref("browser.fixup.alternate.suffix");
    if (!suffix.endsWith("/")) {
      suffix += "/";
    }

    // trim leading/trailing spaces (bug 233205)
    value = value.trim();

    // Tack www. and suffix on.  If user has appended directories, insert
    // suffix before them (bug 279035).  Be careful not to get two slashes.
    let firstSlash = value.indexOf("/");
    if (firstSlash >= 0) {
      value =
        value.substring(0, firstSlash) +
        suffix +
        value.substring(firstSlash + 1);
    } else {
      value = value + suffix;
    }
    value = "http://www." + value;

    this.value = value;
    return value;
  }

  /**
   * Autofills a value into the input.  The value will be autofilled regardless
   * of the input's current value.
   *
   * @param {string} value
   *   The value to autofill.
   * @param {integer} selectionStart
   *   The new selectionStart.
   * @param {integer} selectionEnd
   *   The new selectionEnd.
   */
  _autofillValue(value, selectionStart, selectionEnd) {
    // The autofilled value may be a URL that includes a scheme at the
    // beginning.  Do not allow it to be trimmed.
    this._setValue(value, false);
    this.selectionStart = selectionStart;
    this.selectionEnd = selectionEnd;
    this._autofillPlaceholder = value;
  }

  /**
   * Loads the url in the appropriate place.
   *
   * @param {string} url
   *   The URL to open.
   * @param {string} openUILinkWhere
   *   Where we expect the result to be opened.
   * @param {object} params
   *   The parameters related to how and where the result will be opened.
   *   Further supported paramters are listed in utilityOverlay.js#openUILinkIn.
   * @param {object} params.triggeringPrincipal
   *   The principal that the action was triggered from.
   * @param {nsIInputStream} [params.postData]
   *   The POST data associated with a search submission.
   * @param {boolean} [params.allowInheritPrincipal]
   *   Whether the principal can be inherited.
   * @param {object} [resultDetails]
   *   Details of the selected result, if any.
   * @param {UrlbarUtils.RESULT_TYPE} [result.type]
   *   Details of the result type, if any.
   * @param {UrlbarUtils.RESULT_SOURCE} [result.source]
   *   Details of the result source, if any.
   * @param {object} browser [optional] the browser to use for the load.
   */
  _loadURL(
    url,
    openUILinkWhere,
    params,
    resultDetails = null,
    browser = this.window.gBrowser.selectedBrowser
  ) {
    // No point in setting these because we'll handleRevert() a few rows below.
    if (openUILinkWhere == "current") {
      this.value = url;
      browser.userTypedValue = url;
    }

    // No point in setting this if we are loading in a new window.
    if (
      openUILinkWhere != "window" &&
      this.window.gInitialPages.includes(url)
    ) {
      browser.initialPageLoadedFromUserAction = url;
    }

    try {
      UrlbarUtils.addToUrlbarHistory(url, this.window);
    } catch (ex) {
      // Things may go wrong when adding url to session history,
      // but don't let that interfere with the loading of the url.
      Cu.reportError(ex);
    }

    // Reset DOS mitigations for the basic auth prompt.
    // TODO: When bug 1498553 is resolved, we should be able to
    // remove the !triggeringPrincipal condition here.
    if (
      !params.triggeringPrincipal ||
      params.triggeringPrincipal.isSystemPrincipal
    ) {
      delete browser.authPromptAbuseCounter;
    }

    params.allowThirdPartyFixup = true;

    if (openUILinkWhere == "current") {
      params.targetBrowser = browser;
      params.indicateErrorPageLoad = true;
      params.allowPinnedTabHostChange = true;
      params.allowPopups = url.startsWith("javascript:");
    } else {
      params.initiatingDoc = this.window.document;
    }

    // Focus the content area before triggering loads, since if the load
    // occurs in a new tab, we want focus to be restored to the content
    // area when the current tab is re-selected.
    browser.focus();

    if (openUILinkWhere != "current") {
      this.handleRevert();
    }

    // Notify about the start of navigation.
    this._notifyStartNavigation(resultDetails);

    try {
      this.window.openTrustedLinkIn(url, openUILinkWhere, params);
    } catch (ex) {
      // This load can throw an exception in certain cases, which means
      // we'll want to replace the URL with the loaded URL:
      if (ex.result != Cr.NS_ERROR_LOAD_SHOWED_ERRORPAGE) {
        this.handleRevert();
      }
    }

    // Make sure the domain name stays visible for spoof protection and usability.
    this.selectionStart = this.selectionEnd = 0;

    this.view.close();
  }

  /**
   * Determines where a URL/page should be opened.
   *
   * @param {Event} event the event triggering the opening.
   * @returns {"current" | "tabshifted" | "tab" | "save" | "window"}
   */
  _whereToOpen(event) {
    let isMouseEvent = event instanceof MouseEvent;
    let reuseEmpty = !isMouseEvent;
    let where = undefined;
    if (
      !isMouseEvent &&
      event &&
      (event.altKey || event.getModifierState("AltGraph"))
    ) {
      // We support using 'alt' to open in a tab, because ctrl/shift
      // might be used for canonizing URLs:
      where = event.shiftKey ? "tabshifted" : "tab";
    } else if (
      !isMouseEvent &&
      event &&
      event.ctrlKey &&
      UrlbarPrefs.get("ctrlCanonizesURLs")
    ) {
      // If we're allowing canonization, and this is a key event with ctrl
      // pressed, open in current tab to allow ctrl-enter to canonize URL.
      where = "current";
    } else {
      where = this.window.whereToOpenLink(event, false, false);
    }
    if (UrlbarPrefs.get("openintab")) {
      if (where == "current") {
        where = "tab";
      } else if (where == "tab") {
        where = "current";
      }
      reuseEmpty = true;
    }
    if (
      where == "tab" &&
      reuseEmpty &&
      this.window.gBrowser.selectedTab.isEmpty
    ) {
      where = "current";
    }
    return where;
  }

  _initCopyCutController() {
    this._copyCutController = new CopyCutController(this);
    this.inputField.controllers.insertControllerAt(0, this._copyCutController);
  }

  _initPasteAndGo() {
    let inputBox = this.querySelector("moz-input-box");
    let contextMenu = inputBox.menupopup;
    let insertLocation = contextMenu.firstElementChild;
    while (
      insertLocation.nextElementSibling &&
      insertLocation.getAttribute("cmd") != "cmd_paste"
    ) {
      insertLocation = insertLocation.nextElementSibling;
    }
    if (!insertLocation) {
      return;
    }

    let pasteAndGo = this.document.createXULElement("menuitem");
    let label = Services.strings
      .createBundle("chrome://browser/locale/browser.properties")
      .GetStringFromName("pasteAndGo.label");
    pasteAndGo.setAttribute("label", label);
    pasteAndGo.setAttribute("anonid", "paste-and-go");
    pasteAndGo.addEventListener("command", () => {
      this._suppressStartQuery = true;

      this.select();
      this.window.goDoCommand("cmd_paste");
      this.handleCommand();

      this._suppressStartQuery = false;
    });

    contextMenu.addEventListener("popupshowing", () => {
      // Close the results pane when the input field contextual menu is open,
      // because paste and go doesn't want a result selection.
      this.view.close();

      let controller = this.document.commandDispatcher.getControllerForCommand(
        "cmd_paste"
      );
      let enabled = controller.isCommandEnabled("cmd_paste");
      if (enabled) {
        pasteAndGo.removeAttribute("disabled");
      } else {
        pasteAndGo.setAttribute("disabled", "true");
      }
    });

    insertLocation.insertAdjacentElement("afterend", pasteAndGo);
  }

  /**
   * This notifies observers that the user has entered or selected something in
   * the URL bar which will cause navigation.
   *
   * We use the observer service, so that we don't need to load extra facilities
   * if they aren't being used, e.g. WebNavigation.
   *
   * @param {UrlbarResult} result
   *   Details of the result that was selected, if any.
   */
  _notifyStartNavigation(result) {
    Services.obs.notifyObservers({ result }, "urlbar-user-start-navigation");
  }

  /**
   * Determines if we should select all the text in the Urlbar based on the
   *  Urlbar state, and whether the selection is empty.
   */
  _maybeSelectAll() {
    if (
      !this._preventClickSelectsAll &&
      this._compositionState != UrlbarUtils.COMPOSITION.COMPOSING &&
      this.document.activeElement == this.inputField &&
      this.inputField.selectionStart == this.inputField.selectionEnd
    ) {
      this.select();
    }
  }

  // Event handlers below.

  _on_blur(event) {
    this.focusedViaMousedown = false;
    this.removeAttribute("focused");
    this.endLayoutExtend();

    if (this._autofillPlaceholder && this.window.gBrowser.userTypedValue) {
      // Restore value to the last typed one, removing any autofilled portion.
      this.value = this.window.gBrowser.userTypedValue;
    }

    this.formatValue();
    this._resetSearchState();

    // In certain cases, like holding an override key and confirming an entry,
    // we don't key a keyup event for the override key, thus we make this
    // additional cleanup on blur.
    this._clearActionOverride();

    // The extension input sessions depends more on blur than on the fact we
    // actually cancel a running query, so we do it here.
    if (ExtensionSearchHandler.hasActiveInputSession()) {
      ExtensionSearchHandler.handleInputCancelled();
    }

    // Respect the autohide preference for easier inspecting/debugging via
    // the browser toolbox.
    if (!UrlbarPrefs.get("ui.popup.disable_autohide")) {
      this.view.close();
    }

    // We may have hidden popup notifications, show them again if necessary.
    if (this.getAttribute("pageproxystate") != "valid") {
      this.window.UpdatePopupNotificationsVisibility();
    }
  }

  _on_click(event) {
    if (
      event.target == this.inputField ||
      event.target == this._inputContainer
    ) {
      this._maybeSelectAll();
    }
  }

  _on_contextmenu(event) {
    // Context menu opened via keyboard shortcut.
    if (!event.button) {
      return;
    }

    this._maybeSelectAll();
  }

  _on_focus(event) {
    if (!this._hideFocus) {
      this.setAttribute("focused", "true");
    }

    this.startLayoutExtend();

    if (this.focusedViaMousedown) {
      this.view.autoOpen({ event });
    } else if (this.inputField.hasAttribute("refocused-by-panel")) {
      this._maybeSelectAll();
    }

    this._updateUrlTooltip();
    this.formatValue();

    // Hide popup notifications, to reduce visual noise.
    if (this.getAttribute("pageproxystate") != "valid") {
      this.window.UpdatePopupNotificationsVisibility();
    }
  }

  _on_mouseover(event) {
    this._updateUrlTooltip();
  }

  _on_mousedown(event) {
    switch (event.currentTarget) {
      case this.textbox:
        this._mousedownOnUrlbarDescendant = true;

        if (
          event.target != this.inputField &&
          event.target != this._inputContainer
        ) {
          break;
        }

        this.focusedViaMousedown = !this.focused;
        this._preventClickSelectsAll = this.focused;

        if (event.target == this._inputContainer) {
          this.focus();
        }

        // The rest of this case only cares about left clicks.
        if (event.button != 0) {
          break;
        }

        // Clear any previous selection unless we are focused, to ensure it
        // doesn't affect drag selection.
        if (this.focusedViaMousedown) {
          this.selectionStart = this.selectionEnd = 0;
        }

        this.view.autoOpen();
        break;
      case this.dropmarker:
        if (event.button != 0) {
          break;
        }

        if (this.view.isOpen) {
          this.view.close();
        } else {
          this.focus();
          this.startQuery({
            allowAutofill: false,
          });
          this._maybeSelectAll();
        }
        break;
      case this.window:
        if (this._mousedownOnUrlbarDescendant) {
          this._mousedownOnUrlbarDescendant = false;
          break;
        }
        // Don't close the view when clicking on a tab; we may want to keep the
        // view open on tab switch, and the TabSelect event arrived earlier.
        if (event.target.closest("tab")) {
          break;
        }
        // Close the view when clicking on toolbars and other UI pieces that
        // might not automatically remove focus from the input.
        // Respect the autohide preference for easier inspecting/debugging via
        // the browser toolbox.
        if (!UrlbarPrefs.get("ui.popup.disable_autohide")) {
          this.view.close();
        }
        break;
    }
  }

  _on_input(event) {
    let value = this.value;
    this.valueIsTyped = true;
    this._untrimmedValue = value;
    this.window.gBrowser.userTypedValue = value;

    let compositionState = this._compositionState;
    let compositionClosedPopup = this._compositionClosedPopup;

    // Clear composition values if we're no more composing.
    if (this._compositionState != UrlbarUtils.COMPOSITION.COMPOSING) {
      this._compositionState = UrlbarUtils.COMPOSITION.NONE;
      this._compositionClosedPopup = false;
    }

    if (value) {
      this.setAttribute("usertyping", "true");
    } else {
      this.removeAttribute("usertyping");
    }
    this.removeAttribute("actiontype");

    if (!this.view.isOpen || !value) {
      this.view.clear();
    }
    if (this.view.isOpen && !value) {
      this.view.close();
      return;
    }

    this.view.removeAccessibleFocus();

    // During composition with an IME, the following events happen in order:
    // 1. a compositionstart event
    // 2. some input events
    // 3. a compositionend event
    // 4. an input event

    // We should do nothing during composition or if composition was canceled
    // and we didn't close the popup on composition start.
    if (
      compositionState == UrlbarUtils.COMPOSITION.COMPOSING ||
      (compositionState == UrlbarUtils.COMPOSITION.CANCELED &&
        !compositionClosedPopup)
    ) {
      return;
    }

    // Autofill only when text is inserted (i.e., event.data is not empty) and
    // it's not due to pasting.
    let allowAutofill =
      !!event.data &&
      !event.inputType.startsWith("insertFromPaste") &&
      event.inputType != "insertFromYank" &&
      this._maybeAutofillOnInput(value);

    this.startQuery({
      searchString: value,
      allowAutofill,
      resetSearchState: false,
    });
  }

  _on_select(event) {
    // On certain user input, AutoCopyListener::OnSelectionChange() updates
    // the primary selection with user-selected text (when supported).
    // Selection::NotifySelectionListeners() then dispatches a "select" event
    // under similar conditions via TextInputListener::OnSelectionChange().
    // This event is received here in order to replace the primary selection
    // from the editor with text having the adjustments of
    // _getSelectedValueForClipboard(), such as adding the scheme for the url.
    //
    // Other "select" events are also received, however, and must be excluded.
    if (
      // _suppressPrimaryAdjustment is set during select().  Don't update
      // the primary selection because that is not the intent of user input,
      // which may be new tab or urlbar focus.
      this._suppressPrimaryAdjustment ||
      // The check on isHandlingUserInput filters out async "select" events
      // from setSelectionRange(), which occur when autofill text is selected.
      !this.window.windowUtils.isHandlingUserInput ||
      !Services.clipboard.supportsSelectionClipboard()
    ) {
      return;
    }

    let val = this._getSelectedValueForClipboard();
    if (!val) {
      return;
    }

    ClipboardHelper.copyStringToClipboard(
      val,
      Services.clipboard.kSelectionClipboard
    );
  }

  _on_overflow(event) {
    const targetIsPlaceholder =
      event.originalTarget.implementedPseudoElement == "::placeholder";
    // We only care about the non-placeholder text.
    // This shouldn't be needed, see bug 1487036.
    if (targetIsPlaceholder) {
      return;
    }
    this._overflowing = true;
    this._updateTextOverflow();
  }

  _on_underflow(event) {
    const targetIsPlaceholder =
      event.originalTarget.implementedPseudoElement == "::placeholder";
    // We only care about the non-placeholder text.
    // This shouldn't be needed, see bug 1487036.
    if (targetIsPlaceholder) {
      return;
    }
    this._overflowing = false;

    this._updateTextOverflow();

    this._updateUrlTooltip();
  }

  _on_paste(event) {
    let originalPasteData = event.clipboardData.getData("text/plain");
    if (!originalPasteData) {
      return;
    }

    let oldValue = this.inputField.value;
    let oldStart = oldValue.substring(0, this.selectionStart);
    // If there is already non-whitespace content in the URL bar
    // preceding the pasted content, it's not necessary to check
    // protocols used by the pasted content:
    if (oldStart.trim()) {
      return;
    }
    let oldEnd = oldValue.substring(this.selectionEnd);

    let pasteData = UrlbarUtils.stripUnsafeProtocolOnPaste(originalPasteData);
    if (originalPasteData != pasteData) {
      // Unfortunately we're not allowed to set the bits being pasted
      // so cancel this event:
      event.preventDefault();
      event.stopImmediatePropagation();

      this.inputField.value = oldStart + pasteData + oldEnd;
      // Fix up cursor/selection:
      let newCursorPos = oldStart.length + pasteData.length;
      this.selectionStart = newCursorPos;
      this.selectionEnd = newCursorPos;
    }
  }

  _on_scrollend(event) {
    this._updateTextOverflow();
  }

  _on_TabSelect(event) {
    this._gotTabSelect = true;
    this._afterTabSelectAndFocusChange();
  }

  _on_keydown(event) {
    // Due to event deferring, it's possible preventDefault() won't be invoked
    // soon enough to actually prevent some of the default behaviors, thus we
    // have to handle the event "twice". This first immediate call passes false
    // as second argument so that handleKeyNavigation will only simulate the
    // event handling, without actually executing actions.
    // TODO (Bug 1541806): improve this handling, maybe by delaying actions
    // instead of events.
    if (this.eventBufferer.shouldDeferEvent(event)) {
      this.controller.handleKeyNavigation(event, false);
    }
    this._toggleActionOverride(event);
    this.eventBufferer.maybeDeferEvent(event, () => {
      this.controller.handleKeyNavigation(event);
    });
  }

  _on_keyup(event) {
    this._toggleActionOverride(event);
  }

  _on_compositionstart(event) {
    if (this._compositionState == UrlbarUtils.COMPOSITION.COMPOSING) {
      throw new Error("Trying to start a nested composition?");
    }
    this._compositionState = UrlbarUtils.COMPOSITION.COMPOSING;

    // Close the view. This will also stop searching.
    if (this.view.isOpen) {
      this._compositionClosedPopup = true;
      this.view.close();
    } else {
      this._compositionClosedPopup = false;
    }
  }

  _on_compositionend(event) {
    if (this._compositionState != UrlbarUtils.COMPOSITION.COMPOSING) {
      throw new Error("Trying to stop a non existing composition?");
    }

    // We can't yet retrieve the committed value from the editor, since it isn't
    // completely committed yet. We'll handle it at the next input event.
    this._compositionState = event.data
      ? UrlbarUtils.COMPOSITION.COMMIT
      : UrlbarUtils.COMPOSITION.CANCELED;
  }

  _on_dragstart(event) {
    // Drag only if the gesture starts from the input field.
    let nodePosition = this.inputField.compareDocumentPosition(
      event.originalTarget
    );
    if (
      event.target != this.inputField &&
      !(nodePosition & Node.DOCUMENT_POSITION_CONTAINED_BY)
    ) {
      return;
    }

    // Don't cover potential drop targets on the toolbars or in content.
    this.view.close();

    // Only customize the drag data if the entire value is selected and it's a
    // loaded URI. Use default behavior otherwise.
    if (
      this.selectionStart != 0 ||
      this.selectionEnd != this.inputField.textLength ||
      this.getAttribute("pageproxystate") != "valid"
    ) {
      return;
    }

    let href = this.window.gBrowser.currentURI.displaySpec;
    let title = this.window.gBrowser.contentTitle || href;

    event.dataTransfer.setData("text/x-moz-url", `${href}\n${title}`);
    event.dataTransfer.setData("text/unicode", href);
    event.dataTransfer.setData("text/html", `<a href="${href}">${title}</a>`);
    event.dataTransfer.effectAllowed = "copyLink";
    event.stopPropagation();
  }

  _on_dragover(event) {
    if (!getDroppableData(event)) {
      event.dataTransfer.dropEffect = "none";
    }
  }

  _on_drop(event) {
    let droppedItem = getDroppableData(event);
    let droppedURL =
      droppedItem instanceof URL ? droppedItem.href : droppedItem;
    if (droppedURL && droppedURL !== this.window.gBrowser.currentURI.spec) {
      let principal = Services.droppedLinkHandler.getTriggeringPrincipal(event);
      this.value = droppedURL;
      this.window.SetPageProxyState("invalid");
      this.focus();
      this.handleCommand(null, undefined, undefined, principal);
      // For safety reasons, in the drop case we don't want to immediately show
      // the the dropped value, instead we want to keep showing the current page
      // url until an onLocationChange happens.
      // See the handling in URLBarSetURI for further details.
      this.window.gBrowser.userTypedValue = null;
      this.window.URLBarSetURI(null, true);
    }
  }

  _on_customizationstarting() {
    this.blur();

    this.inputField.controllers.removeController(this._copyCutController);
    delete this._copyCutController;
  }

  _on_aftercustomization() {
    this._initCopyCutController();
    this._initPasteAndGo();
  }

  _on_unload() {
    // We can remove this once UrlbarPrefs has support for listeners.
    // (bug 1560013)
    Services.prefs.removeObserver("browser.urlbar.openViewOnFocus", this);
  }
}

/**
 * Tries to extract droppable data from a DND event.
 * @param {Event} event The DND event to examine.
 * @returns {URL|string|null}
 *          null if there's a security reason for which we should do nothing.
 *          A URL object if it's a value we can load.
 *          A string value otherwise.
 */
function getDroppableData(event) {
  let links;
  try {
    links = Services.droppedLinkHandler.dropLinks(event);
  } catch (ex) {
    // This is either an unexpected failure or a security exception; in either
    // case we should always return null.
    return null;
  }
  // The URL bar automatically handles inputs with newline characters,
  // so we can get away with treating text/x-moz-url flavours as text/plain.
  if (links.length && links[0].url) {
    event.preventDefault();
    let href = links[0].url;
    if (UrlbarUtils.stripUnsafeProtocolOnPaste(href) != href) {
      // We may have stripped an unsafe protocol like javascript: and if so
      // there's no point in handling a partial drop.
      event.stopImmediatePropagation();
      return null;
    }

    try {
      // If this throws, urlSecurityCheck would also throw, as that's what it
      // does with things that don't pass the IO service's newURI constructor
      // without fixup. It's conceivable we may want to relax this check in
      // the future (so e.g. www.foo.com gets fixed up), but not right now.
      let url = new URL(href);
      // If we succeed, try to pass security checks. If this works, return the
      // URL object. If the *security checks* fail, return null.
      try {
        let principal = Services.droppedLinkHandler.getTriggeringPrincipal(
          event
        );
        BrowserUtils.urlSecurityCheck(
          url,
          principal,
          Ci.nsIScriptSecurityManager.DISALLOW_INHERIT_PRINCIPAL
        );
        return url;
      } catch (ex) {
        return null;
      }
    } catch (ex) {
      // We couldn't make a URL out of this. Continue on, and return text below.
    }
  }
  // Handle as text.
  return event.dataTransfer.getData("text/unicode");
}

/**
 * Handles copy and cut commands for the urlbar.
 */
class CopyCutController {
  /**
   * @param {UrlbarInput} urlbar
   *   The UrlbarInput instance to use this controller for.
   */
  constructor(urlbar) {
    this.urlbar = urlbar;
  }

  /**
   * @param {string} command
   *   The name of the command to handle.
   */
  doCommand(command) {
    let urlbar = this.urlbar;
    let val = urlbar._getSelectedValueForClipboard();
    if (!val) {
      return;
    }

    if (command == "cmd_cut" && this.isCommandEnabled(command)) {
      let start = urlbar.selectionStart;
      let end = urlbar.selectionEnd;
      urlbar.inputField.value =
        urlbar.inputField.value.substring(0, start) +
        urlbar.inputField.value.substring(end);
      urlbar.selectionStart = urlbar.selectionEnd = start;

      let event = urlbar.document.createEvent("UIEvents");
      event.initUIEvent("input", true, false, urlbar.window, 0);
      urlbar.inputField.dispatchEvent(event);
    }

    ClipboardHelper.copyString(val);
  }

  /**
   * @param {string} command
   * @returns {boolean}
   *   Whether the command is handled by this controller.
   */
  supportsCommand(command) {
    switch (command) {
      case "cmd_copy":
      case "cmd_cut":
        return true;
    }
    return false;
  }

  /**
   * @param {string} command
   * @returns {boolean}
   *   Whether the command should be enabled.
   */
  isCommandEnabled(command) {
    return (
      this.supportsCommand(command) &&
      (command != "cmd_cut" || !this.urlbar.readOnly) &&
      this.urlbar.selectionStart < this.urlbar.selectionEnd
    );
  }

  onEvent() {}
}
