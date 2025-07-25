/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// @flow

import PropTypes from "prop-types";
import React, { PureComponent } from "react";
import { bindActionCreators } from "redux";
import ReactDOM from "react-dom";
import { connect } from "../../utils/connect";
import classnames from "classnames";
import { debounce } from "lodash";

import { isFirefox } from "devtools-environment";
import { getLineText } from "./../../utils/source";
import { features } from "../../utils/prefs";
import { getIndentation } from "../../utils/indentation";

import { showMenu } from "devtools-contextmenu";
import {
  createBreakpointItems,
  breakpointItemActions,
} from "./menus/breakpoints";

import { continueToHereItem, editorItemActions } from "./menus/editor";

import type { BreakpointItemActions } from "./menus/breakpoints";
import type { EditorItemActions } from "./menus/editor";

import {
  getActiveSearch,
  getSelectedLocation,
  getSelectedSourceWithContent,
  getConditionalPanelLocation,
  getSymbols,
  getIsPaused,
  getCurrentThread,
  getThreadContext,
  getSkipPausing,
  getInlinePreview,
} from "../../selectors";

// Redux actions
import actions from "../../actions";

import SearchBar from "./SearchBar";
import HighlightLines from "./HighlightLines";
import Preview from "./Preview";
import Breakpoints from "./Breakpoints";
import ColumnBreakpoints from "./ColumnBreakpoints";
import DebugLine from "./DebugLine";
import HighlightLine from "./HighlightLine";
import EmptyLines from "./EmptyLines";
import EditorMenu from "./EditorMenu";
import ConditionalPanel from "./ConditionalPanel";
import InlinePreviews from "./InlinePreviews";

import {
  showSourceText,
  showLoading,
  showErrorMessage,
  getEditor,
  clearEditor,
  getCursorLine,
  getCursorColumn,
  lineAtHeight,
  toSourceLine,
  getDocument,
  scrollToColumn,
  toEditorPosition,
  getSourceLocationFromMouseEvent,
  hasDocument,
  onMouseOver,
  startOperation,
  endOperation,
} from "../../utils/editor";

import { resizeToggleButton, resizeBreakpointGutter } from "../../utils/ui";

import "./Editor.css";
import "./Breakpoints.css";
import "./Highlight.css";
import "./InlinePreview.css";

import type SourceEditor from "../../utils/editor/source-editor";
import type { SymbolDeclarations } from "../../workers/parser";
import type {
  SourceLocation,
  SourceWithContent,
  ThreadContext,
} from "../../types";

const cssVars = {
  searchbarHeight: "var(--editor-searchbar-height)",
};

type OwnProps = {|
  startPanelSize: number,
  endPanelSize: number,
|};
export type Props = {
  cx: ThreadContext,
  selectedLocation: ?SourceLocation,
  selectedSource: ?SourceWithContent,
  searchOn: boolean,
  startPanelSize: number,
  endPanelSize: number,
  conditionalPanelLocation: SourceLocation,
  symbols: SymbolDeclarations,
  isPaused: boolean,
  skipPausing: boolean,
  inlinePreviewEnabled: boolean,

  // Actions
  openConditionalPanel: typeof actions.openConditionalPanel,
  closeConditionalPanel: typeof actions.closeConditionalPanel,
  continueToHere: typeof actions.continueToHere,
  addBreakpointAtLine: typeof actions.addBreakpointAtLine,
  jumpToMappedLocation: typeof actions.jumpToMappedLocation,
  toggleBreakpointAtLine: typeof actions.toggleBreakpointAtLine,
  traverseResults: typeof actions.traverseResults,
  updateViewport: typeof actions.updateViewport,
  updateCursorPosition: typeof actions.updateCursorPosition,
  closeTab: typeof actions.closeTab,
  breakpointActions: BreakpointItemActions,
  editorActions: EditorItemActions,
  toggleBlackBox: typeof actions.toggleBlackBox,
};

type State = {
  editor: SourceEditor,
  contextMenu: ?MouseEvent,
};

class Editor extends PureComponent<Props, State> {
  $editorWrapper: ?HTMLDivElement;
  constructor(props: Props) {
    super(props);

    this.state = {
      highlightedLineRange: null,
      editor: (null: any),
      contextMenu: null,
    };
  }

  componentWillReceiveProps(nextProps: Props) {
    let { editor } = this.state;

    if (!editor && nextProps.selectedSource) {
      editor = this.setupEditor();
    }

    startOperation();
    this.setText(nextProps, editor);
    this.setSize(nextProps, editor);
    this.scrollToLocation(nextProps, editor);
    endOperation();

    if (this.props.selectedSource != nextProps.selectedSource) {
      this.props.updateViewport();
      resizeBreakpointGutter(editor.codeMirror);
      resizeToggleButton(editor.codeMirror);
    }
  }

  setupEditor() {
    const editor = getEditor();

    // disables the default search shortcuts
    // $FlowIgnore
    editor._initShortcuts = () => {};

    const node = ReactDOM.findDOMNode(this);
    if (node instanceof HTMLElement) {
      editor.appendToLocalElement(node.querySelector(".editor-mount"));
    }

    const { codeMirror } = editor;
    const codeMirrorWrapper = codeMirror.getWrapperElement();

    codeMirror.on("gutterClick", this.onGutterClick);

    // Set code editor wrapper to be focusable
    codeMirrorWrapper.tabIndex = 0;
    codeMirrorWrapper.addEventListener("keydown", e => this.onKeyDown(e));
    codeMirrorWrapper.addEventListener("click", e => this.onClick(e));
    codeMirrorWrapper.addEventListener("mouseover", onMouseOver(codeMirror));

    const toggleFoldMarkerVisibility = e => {
      if (node instanceof HTMLElement) {
        node
          .querySelectorAll(".CodeMirror-guttermarker-subtle")
          .forEach(elem => {
            elem.classList.toggle("visible");
          });
      }
    };

    const codeMirrorGutter = codeMirror.getGutterElement();
    codeMirrorGutter.addEventListener("mouseleave", toggleFoldMarkerVisibility);
    codeMirrorGutter.addEventListener("mouseenter", toggleFoldMarkerVisibility);

    if (!isFirefox()) {
      codeMirror.on("gutterContextMenu", (cm, line, eventName, event) =>
        this.onGutterContextMenu(event)
      );
      codeMirror.on("contextmenu", (cm, event) => this.openMenu(event));
    } else {
      codeMirrorWrapper.addEventListener("contextmenu", event =>
        this.openMenu(event)
      );
    }

    codeMirror.on("scroll", this.onEditorScroll);
    this.onEditorScroll();
    this.setState({ editor });
    return editor;
  }

  componentDidMount() {
    const { shortcuts } = this.context;

    shortcuts.on(L10N.getStr("toggleBreakpoint.key"), this.onToggleBreakpoint);
    shortcuts.on(
      L10N.getStr("toggleCondPanel.breakpoint.key"),
      this.onToggleConditionalPanel
    );
    shortcuts.on(
      L10N.getStr("toggleCondPanel.logPoint.key"),
      this.onToggleConditionalPanel
    );
    shortcuts.on(L10N.getStr("sourceTabs.closeTab.key"), this.onClosePress);
    shortcuts.on("Esc", this.onEscape);
  }

  onClosePress = (key: mixed, e: KeyboardEvent) => {
    const { cx, selectedSource } = this.props;
    if (selectedSource) {
      e.preventDefault();
      e.stopPropagation();
      this.props.closeTab(cx, selectedSource);
    }
  };

  componentWillUnmount() {
    const { editor } = this.state;
    if (editor) {
      editor.destroy();
      editor.codeMirror.off("scroll", this.onEditorScroll);
      this.setState({ editor: (null: any) });
    }

    const { shortcuts } = this.context;
    shortcuts.off(L10N.getStr("sourceTabs.closeTab.key"));
    shortcuts.off(L10N.getStr("toggleBreakpoint.key"));
    shortcuts.off(L10N.getStr("toggleCondPanel.breakpoint.key"));
    shortcuts.off(L10N.getStr("toggleCondPanel.logPoint.key"));
  }

  getCurrentLine() {
    const { codeMirror } = this.state.editor;
    const { selectedSource } = this.props;
    if (!selectedSource) {
      return;
    }

    const line = getCursorLine(codeMirror);
    return toSourceLine(selectedSource.id, line);
  }

  onToggleBreakpoint = (key: mixed, e: KeyboardEvent) => {
    e.preventDefault();
    e.stopPropagation();

    const line = this.getCurrentLine();
    if (typeof line !== "number") {
      return;
    }

    this.props.toggleBreakpointAtLine(this.props.cx, line);
  };

  onToggleConditionalPanel = (key: mixed, e: KeyboardEvent) => {
    e.stopPropagation();
    e.preventDefault();

    const {
      conditionalPanelLocation,
      closeConditionalPanel,
      openConditionalPanel,
      selectedSource,
    } = this.props;

    const line = this.getCurrentLine();

    const { codeMirror } = this.state.editor;
    // add one to column for correct position in editor.
    const column = getCursorColumn(codeMirror) + 1;

    if (conditionalPanelLocation) {
      return closeConditionalPanel();
    }

    if (!selectedSource) {
      return;
    }

    if (typeof line !== "number") {
      return;
    }

    return openConditionalPanel(
      {
        line,
        column,
        sourceId: selectedSource.id,
      },
      false
    );
  };

  onEditorScroll = debounce(this.props.updateViewport, 75);

  onKeyDown(e: KeyboardEvent) {
    const { codeMirror } = this.state.editor;
    const { key, target } = e;
    const codeWrapper = codeMirror.getWrapperElement();
    const textArea = codeWrapper.querySelector("textArea");

    if (key === "Escape" && target == textArea) {
      e.stopPropagation();
      e.preventDefault();
      codeWrapper.focus();
    } else if (key === "Enter" && target == codeWrapper) {
      e.preventDefault();
      // Focus into editor's text area
      textArea.focus();
    }
  }

  /*
   * The default Esc command is overridden in the CodeMirror keymap to allow
   * the Esc keypress event to be catched by the toolbox and trigger the
   * split console. Restore it here, but preventDefault if and only if there
   * is a multiselection.
   */
  onEscape = (key: mixed, e: KeyboardEvent) => {
    if (!this.state.editor) {
      return;
    }

    const { codeMirror } = this.state.editor;
    if (codeMirror.listSelections().length > 1) {
      codeMirror.execCommand("singleSelection");
      e.preventDefault();
    }
  };

  openMenu(event: MouseEvent) {
    event.stopPropagation();
    event.preventDefault();

    const {
      cx,
      selectedSource,
      breakpointActions,
      editorActions,
      isPaused,
      conditionalPanelLocation,
      closeConditionalPanel,
    } = this.props;
    const { editor } = this.state;
    if (!selectedSource || !editor) {
      return;
    }

    // only allow one conditionalPanel location.
    if (conditionalPanelLocation) {
      closeConditionalPanel();
    }

    const target: Element = (event.target: any);
    const { id: sourceId } = selectedSource;
    const line = lineAtHeight(editor, sourceId, event);

    if (typeof line != "number") {
      return;
    }

    const location = { line, column: undefined, sourceId };

    if (target.classList.contains("CodeMirror-linenumber")) {
      const lineText = getLineText(
        sourceId,
        selectedSource.content,
        line
      ).trim();

      return showMenu(event, [
        ...createBreakpointItems(cx, location, breakpointActions, lineText),
        { type: "separator" },
        continueToHereItem(cx, location, isPaused, editorActions),
      ]);
    }

    if (target.getAttribute("id") === "columnmarker") {
      return;
    }

    this.setState({ contextMenu: event });
  }

  clearContextMenu = () => {
    this.setState({ contextMenu: null });
  };

  onGutterClick = (
    cm: Object,
    line: number,
    gutter: string,
    ev: MouseEvent
  ) => {
    const {
      cx,
      selectedSource,
      conditionalPanelLocation,
      closeConditionalPanel,
      addBreakpointAtLine,
      continueToHere,
      toggleBlackBox,
    } = this.props;

    // ignore right clicks in the gutter
    if ((ev.ctrlKey && ev.button === 0) || ev.button === 2 || !selectedSource) {
      return;
    }

    // if user clicks gutter to set breakpoint on blackboxed source, un-blackbox the source.
    if (selectedSource?.isBlackBoxed) {
      toggleBlackBox(cx, selectedSource);
    }

    if (conditionalPanelLocation) {
      return closeConditionalPanel();
    }

    if (gutter === "CodeMirror-foldgutter") {
      return;
    }

    const sourceLine = toSourceLine(selectedSource.id, line);
    if (typeof sourceLine !== "number") {
      return;
    }

    if (ev.metaKey) {
      return continueToHere(cx, sourceLine);
    }

    return addBreakpointAtLine(cx, sourceLine, ev.altKey, ev.shiftKey);
  };

  onGutterContextMenu = (event: MouseEvent) => {
    return this.openMenu(event);
  };

  onClick(e: MouseEvent) {
    const {
      cx,
      selectedSource,
      updateCursorPosition,
      jumpToMappedLocation,
    } = this.props;

    if (selectedSource) {
      const sourceLocation = getSourceLocationFromMouseEvent(
        this.state.editor,
        selectedSource,
        e
      );

      if (e.metaKey && e.altKey) {
        jumpToMappedLocation(cx, sourceLocation);
      }

      updateCursorPosition(sourceLocation);
    }
  }

  shouldScrollToLocation(nextProps: Props, editor: SourceEditor) {
    const { selectedLocation, selectedSource } = this.props;
    if (
      !editor ||
      !nextProps.selectedSource ||
      !nextProps.selectedLocation ||
      !nextProps.selectedLocation.line ||
      !nextProps.selectedSource.content
    ) {
      return false;
    }

    const isFirstLoad =
      (!selectedSource || !selectedSource.content) &&
      nextProps.selectedSource.content;
    const locationChanged = selectedLocation !== nextProps.selectedLocation;
    const symbolsChanged = nextProps.symbols != this.props.symbols;

    return isFirstLoad || locationChanged || symbolsChanged;
  }

  scrollToLocation(nextProps: Props, editor: SourceEditor) {
    const { selectedLocation, selectedSource } = nextProps;

    if (selectedLocation && this.shouldScrollToLocation(nextProps, editor)) {
      let { line, column } = toEditorPosition(selectedLocation);

      if (selectedSource && hasDocument(selectedSource.id)) {
        const doc = getDocument(selectedSource.id);
        const lineText: ?string = doc.getLine(line);
        column = Math.max(column, getIndentation(lineText));
      }

      scrollToColumn(editor.codeMirror, line, column);
    }
  }

  setSize(nextProps: Props, editor: SourceEditor) {
    if (!editor) {
      return;
    }

    if (
      nextProps.startPanelSize !== this.props.startPanelSize ||
      nextProps.endPanelSize !== this.props.endPanelSize
    ) {
      editor.codeMirror.setSize();
    }
  }

  setText(props: Props, editor: ?SourceEditor) {
    const { selectedSource, symbols } = props;

    if (!editor) {
      return;
    }

    // check if we previously had a selected source
    if (!selectedSource) {
      return this.clearEditor();
    }

    if (!selectedSource.content) {
      return showLoading(editor);
    }

    if (selectedSource.content.state === "rejected") {
      let { value } = selectedSource.content;
      if (typeof value !== "string") {
        value = "Unexpected source error";
      }

      return this.showErrorMessage(value);
    }

    return showSourceText(
      editor,
      selectedSource,
      selectedSource.content.value,
      symbols
    );
  }

  clearEditor() {
    const { editor } = this.state;
    if (!editor) {
      return;
    }

    clearEditor(editor);
  }

  showErrorMessage(msg: string) {
    const { editor } = this.state;
    if (!editor) {
      return;
    }

    showErrorMessage(editor, msg);
  }

  getInlineEditorStyles() {
    const { searchOn } = this.props;

    if (searchOn) {
      return {
        height: `calc(100% - ${cssVars.searchbarHeight})`,
      };
    }

    return {
      height: "100%",
    };
  }

  renderItems() {
    const {
      cx,
      selectedSource,
      conditionalPanelLocation,
      isPaused,
      inlinePreviewEnabled,
    } = this.props;
    const { editor, contextMenu } = this.state;

    if (!selectedSource || !editor || !getDocument(selectedSource.id)) {
      return null;
    }

    return (
      <div>
        <DebugLine />
        <HighlightLine />
        <EmptyLines editor={editor} />
        <Breakpoints editor={editor} cx={cx} />
        <Preview editor={editor} editorRef={this.$editorWrapper} />
        <HighlightLines editor={editor} />
        {
          <EditorMenu
            editor={editor}
            contextMenu={contextMenu}
            clearContextMenu={this.clearContextMenu}
            selectedSource={selectedSource}
          />
        }
        {conditionalPanelLocation ? <ConditionalPanel editor={editor} /> : null}
        {features.columnBreakpoints ? (
          <ColumnBreakpoints editor={editor} />
        ) : null}
        {isPaused && inlinePreviewEnabled ? (
          <InlinePreviews editor={editor} selectedSource={selectedSource} />
        ) : null}
      </div>
    );
  }

  renderSearchBar() {
    const { editor } = this.state;

    if (!this.props.selectedSource) {
      return null;
    }

    return <SearchBar editor={editor} />;
  }

  render() {
    const { selectedSource, skipPausing } = this.props;
    return (
      <div
        className={classnames("editor-wrapper", {
          blackboxed: selectedSource?.isBlackBoxed,
          "skip-pausing": skipPausing,
        })}
        ref={c => (this.$editorWrapper = c)}
      >
        <div
          className="editor-mount devtools-monospace"
          style={this.getInlineEditorStyles()}
        />
        {this.renderSearchBar()}
        {this.renderItems()}
      </div>
    );
  }
}

Editor.contextTypes = {
  shortcuts: PropTypes.object,
};

const mapStateToProps = state => {
  const selectedSource = getSelectedSourceWithContent(state);

  return {
    cx: getThreadContext(state),
    selectedLocation: getSelectedLocation(state),
    selectedSource,
    searchOn: getActiveSearch(state) === "file",
    conditionalPanelLocation: getConditionalPanelLocation(state),
    symbols: getSymbols(state, selectedSource),
    isPaused: getIsPaused(state, getCurrentThread(state)),
    skipPausing: getSkipPausing(state),
    inlinePreviewEnabled: getInlinePreview(state),
  };
};

const mapDispatchToProps = dispatch => ({
  ...bindActionCreators(
    {
      openConditionalPanel: actions.openConditionalPanel,
      closeConditionalPanel: actions.closeConditionalPanel,
      continueToHere: actions.continueToHere,
      toggleBreakpointAtLine: actions.toggleBreakpointAtLine,
      addBreakpointAtLine: actions.addBreakpointAtLine,
      jumpToMappedLocation: actions.jumpToMappedLocation,
      traverseResults: actions.traverseResults,
      updateViewport: actions.updateViewport,
      updateCursorPosition: actions.updateCursorPosition,
      closeTab: actions.closeTab,
      toggleBlackBox: actions.toggleBlackBox,
    },
    dispatch
  ),
  breakpointActions: breakpointItemActions(dispatch),
  editorActions: editorItemActions(dispatch),
});

export default connect<Props, OwnProps, _, _, _, _>(
  mapStateToProps,
  mapDispatchToProps
)(Editor);
