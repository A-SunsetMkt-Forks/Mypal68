/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Services = require("Services");
const InspectorUtils = require("InspectorUtils");

loader.lazyRequireGetter(
  this,
  "CssLogic",
  "devtools/server/actors/inspector/css-logic",
  true
);

const INACTIVE_CSS_ENABLED = Services.prefs.getBoolPref(
  "devtools.inspector.inactive.css.enabled",
  false
);

const VISITED_MDN_LINK = "https://developer.mozilla.org/docs/Web/CSS/:visited";
const VISITED_INVALID_PROPERTIES = allCssPropertiesExcept([
  "color",
  "background-color",
  "border-color",
  "border-bottom-color",
  "border-left-color",
  "border-right-color",
  "border-top-color",
  "column-rule-color",
  "outline-color",
]);

class InactivePropertyHelper {
  /**
   * A list of rules for when CSS properties have no effect.
   *
   * In certain situations, CSS properties do not have any effect. A common
   * example is trying to set a width on an inline element like a <span>.
   *
   * There are so many properties in CSS that it's difficult to remember which
   * ones do and don't apply in certain situations. Some are straight-forward
   * like `flex-wrap` only applying to an element that has `display:flex`.
   * Others are less trivial like setting something other than a color on a
   * `:visited` pseudo-class.
   *
   * This file contains "rules" in the form of objects with the following
   * properties:
   * {
   *   invalidProperties:
   *     Array of CSS property names that are inactive if the rule matches.
   *   when:
   *     The rule itself, a JS function used to identify the conditions
   *     indicating whether a property is valid or not.
   *   fixId:
   *     A Fluent id containing a suggested solution to the problem that is
   *     causing a property to be inactive.
   *   msgId:
   *     A Fluent id containing an error message explaining why a property is
   *     inactive in this situation.
   *   numFixProps:
   *     The number of properties we suggest in the fixId string.
   * }
   *
   * If you add a new rule, also add a test for it in:
   * server/tests/mochitest/test_inspector-inactive-property-helper.html
   *
   * The main export is `isPropertyUsed()`, which can be used to check if a
   * property is used or not, and why.
   */
  get VALIDATORS() {
    return [
      // Flex container property used on non-flex container.
      {
        invalidProperties: ["flex-direction", "flex-flow", "flex-wrap"],
        when: () => !this.flexContainer,
        fixId: "inactive-css-not-flex-container-fix",
        msgId: "inactive-css-not-flex-container",
        numFixProps: 2,
      },
      // Flex item property used on non-flex item.
      {
        invalidProperties: [
          "flex",
          "flex-basis",
          "flex-grow",
          "flex-shrink",
          "order",
        ],
        when: () => !this.flexItem,
        fixId: "inactive-css-not-flex-item-fix-2",
        msgId: "inactive-css-not-flex-item",
        numFixProps: 2,
      },
      // Grid container property used on non-grid container.
      {
        invalidProperties: [
          "grid-auto-columns",
          "grid-auto-flow",
          "grid-auto-rows",
          "grid-template",
          "justify-items",
        ],
        when: () => !this.gridContainer,
        fixId: "inactive-css-not-grid-container-fix",
        msgId: "inactive-css-not-grid-container",
        numFixProps: 2,
      },
      // Grid item property used on non-grid item.
      {
        invalidProperties: [
          "grid-area",
          "grid-column",
          "grid-column-end",
          "grid-column-start",
          "grid-row",
          "grid-row-end",
          "grid-row-start",
          "justify-self",
        ],
        when: () => !this.gridItem && !this.isAbsPosGridElement(),
        fixId: "inactive-css-not-grid-item-fix-2",
        msgId: "inactive-css-not-grid-item",
        numFixProps: 2,
      },
      // Grid and flex item properties used on non-grid or non-flex item.
      {
        invalidProperties: ["align-self", "place-self"],
        when: () =>
          !this.gridItem && !this.flexItem && !this.isAbsPosGridElement(),
        fixId: "inactive-css-not-grid-or-flex-item-fix-2",
        msgId: "inactive-css-not-grid-or-flex-item",
        numFixProps: 4,
      },
      // Grid and flex container properties used on non-grid or non-flex container.
      {
        invalidProperties: [
          "align-content",
          "align-items",
          "justify-content",
          "place-content",
          "place-items",
          "row-gap",
          "grid-gap",
          "grid-row-gap",
        ],
        when: () => !this.gridContainer && !this.flexContainer,
        fixId: "inactive-css-not-grid-or-flex-container-fix",
        msgId: "inactive-css-not-grid-or-flex-container",
        numFixProps: 2,
      },
      // column-gap and shorthands used on non-grid or non-flex or non-multi-col container.
      {
        invalidProperties: [
          "column-gap",
          "gap",
          "grid-column-gap",
        ],
        when: () =>
          !this.gridContainer && !this.flexContainer && !this.multiColContainer,
        fixId:
          "inactive-css-not-grid-or-flex-container-or-multicol-container-fix",
        msgId: "inactive-css-not-grid-or-flex-container-or-multicol-container",
        numFixProps: 3,
      },
      // Inline properties used on non-inline-level elements.
      {
        invalidProperties: ["vertical-align"],
        when: () => {
          const { selectorText } = this.cssRule;

          const isFirstLetter =
            selectorText && selectorText.includes("::first-letter");
          const isFirstLine =
            selectorText && selectorText.includes("::first-line");

          return !this.isInlineLevel() && !isFirstLetter && !isFirstLine;
        },
        fixId: "inactive-css-not-inline-or-tablecell-fix",
        msgId: "inactive-css-not-inline-or-tablecell",
        numFixProps: 2,
      },
      // (max-|min-)width used on inline elements, table rows, or row groups.
      {
        invalidProperties: ["max-width", "min-width", "width"],
        when: () => this.nonReplacedInlineBox || this.tableRow || this.rowGroup,
        fixId: "inactive-css-non-replaced-inline-or-table-row-or-row-group-fix",
        msgId: "inactive-css-property-because-of-display",
        numFixProps: 2,
      },
      // (max-|min-)height used on inline elements, table columns, or column groups.
      {
        invalidProperties: ["max-height", "min-height", "height"],
        when: () =>
          this.nonReplacedInlineBox || this.tableColumn || this.columnGroup,
        fixId:
          "inactive-css-non-replaced-inline-or-table-column-or-column-group-fix",
        msgId: "inactive-css-property-because-of-display",
        numFixProps: 1,
      },
      {
        invalidProperties: ["display"],
        when: () =>
          this.isFloated &&
          this.checkResolvedStyle("display", [
            "inline",
            "inline-block",
            "inline-table",
            "inline-flex",
            "inline-grid",
            "table-cell",
            "table-row",
            "table-row-group",
            "table-header-group",
            "table-footer-group",
            "table-column",
            "table-column-group",
            "table-caption",
          ]),
        fixId: "inactive-css-not-display-block-on-floated-fix",
        msgId: "inactive-css-not-display-block-on-floated",
        numFixProps: 2,
      },
      // The property is impossible to override due to :visited restriction.
      {
        invalidProperties: VISITED_INVALID_PROPERTIES,
        when: () => this.isVisitedRule(),
        fixId: "learn-more",
        msgId: "inactive-css-property-is-impossible-to-override-in-visited",
        numFixProps: 1,
        learnMoreURL: VISITED_MDN_LINK,
      },
      // top, right, bottom, left properties used on non positioned boxes.
      {
        invalidProperties: ["top", "right", "bottom", "left"],
        when: () => !this.isPositioned,
        fixId: "inactive-css-position-property-on-unpositioned-box-fix",
        msgId: "inactive-css-position-property-on-unpositioned-box",
        numFixProps: 1,
      },
      // z-index property used on non positioned boxes that are not grid/flex items.
      {
        invalidProperties: ["z-index"],
        when: () => !this.isPositioned && !this.gridItem && !this.flexItem,
        fixId: "inactive-css-position-property-on-unpositioned-box-fix",
        msgId: "inactive-css-position-property-on-unpositioned-box",
        numFixProps: 1,
      },
    ];
  }

  /**
   * Get a list of unique CSS property names for which there are checks
   * for used/unused state.
   *
   * @return {Set}
   *         List of CSS properties
   */
  get invalidProperties() {
    if (!this._invalidProperties) {
      const allProps = this.VALIDATORS.map(v => v.invalidProperties).flat();
      this._invalidProperties = new Set(allProps);
    }

    return this._invalidProperties;
  }

  /**
   * Is this CSS property having any effect on this element?
   *
   * @param {DOMNode} el
   *        The DOM element.
   * @param {Style} elStyle
   *        The computed style for this DOMNode.
   * @param {DOMRule} cssRule
   *        The CSS rule the property is defined in.
   * @param {String} property
   *        The CSS property name.
   *
   * @return {Object} object
   * @return {String} object.display
   *         The element computed display value.
   * @return {String} object.fixId
   *         A Fluent id containing a suggested solution to the problem that is
   *         causing a property to be inactive.
   * @return {String} object.msgId
   *         A Fluent id containing an error message explaining why a property
   *         is inactive in this situation.
   * @return {Integer} object.numFixProps
   *         The number of properties we suggest in the fixId string.
   * @return {String} object.property
   *         The inactive property name.
   * @return {String} object.learnMoreURL
   *         An optional link if we need to open an other link than
   *         the default MDN property one.
   * @return {Boolean} object.used
   *         true if the property is used.
   */
  isPropertyUsed(el, elStyle, cssRule, property) {
    // Assume the property is used when:
    // - the Inactive CSS pref is not enabled
    // - the property is not in the list of properties to check
    if (!INACTIVE_CSS_ENABLED || !this.invalidProperties.has(property)) {
      return { used: true };
    }

    let fixId = "";
    let msgId = "";
    let numFixProps = 0;
    let learnMoreURL = null;
    let used = true;

    this.VALIDATORS.some(validator => {
      // First check if this rule cares about this property.
      let isRuleConcerned = false;

      if (validator.invalidProperties) {
        isRuleConcerned = validator.invalidProperties.includes(property);
      }

      if (!isRuleConcerned) {
        return false;
      }

      this.select(el, elStyle, cssRule, property);

      // And then run the validator, gathering the error message if the
      // validator passes.
      if (validator.when()) {
        fixId = validator.fixId;
        msgId = validator.msgId;
        numFixProps = validator.numFixProps;
        learnMoreURL = validator.learnMoreURL;
        used = false;

        return true;
      }

      return false;
    });

    this.unselect();

    // Accessing elStyle might throws, we wrap it in a try/catch block to avoid test
    // failures.
    let display;
    try {
      display = elStyle ? elStyle.display : null;
    } catch (e) {}

    return {
      display,
      fixId,
      msgId,
      numFixProps,
      property,
      learnMoreURL,
      used,
    };
  }

  /**
   * Focus on a node.
   *
   * @param {DOMNode} node
   *        Node to focus on.
   */
  select(node, style, cssRule, property) {
    this._node = node;
    this._cssRule = cssRule;
    this._property = property;
    this._style = style;
  }

  /**
   * Clear references to avoid leaks.
   */
  unselect() {
    this._node = null;
    this._cssRule = null;
    this._property = null;
    this._style = null;
  }

  /**
   * Provide a public reference to node.
   */
  get node() {
    return this._node;
  }

  /**
   * Cache and provide node's computed style.
   */
  get style() {
    return this._style;
  }

  /**
   *  Provide a public reference to the css rule.
   */
  get cssRule() {
    return this._cssRule;
  }

  /**
   * Check if the current node's propName is set to one of the values passed in
   * the values array.
   *
   * @param {String} propName
   *        Property name to check.
   * @param {Array} values
   *        Values to compare against.
   */
  checkComputedStyle(propName, values) {
    if (!this.style) {
      return false;
    }
    return values.some(value => this.style[propName] === value);
  }

  /**
   * Check if a rule's propName is set to one of the values passed in the values
   * array.
   *
   * @param {String} propName
   *        Property name to check.
   * @param {Array} values
   *        Values to compare against.
   */
  checkResolvedStyle(propName, values) {
    if (!(this.cssRule && this.cssRule.style)) {
      return false;
    }
    const { style } = this.cssRule;

    return values.some(value => style[propName] === value);
  }

  /**
   *  Check if the current node is an inline-level box.
   */
  isInlineLevel() {
    return this.checkComputedStyle("display", [
      "inline",
      "inline-block",
      "inline-table",
      "inline-flex",
      "inline-grid",
      "table-cell",
      "table-row",
      "table-row-group",
      "table-header-group",
      "table-footer-group",
    ]);
  }

  /**
   * Check if the current node is a flex container i.e. a node that has a style
   * of `display:flex` or `display:inline-flex`.
   */
  get flexContainer() {
    return this.checkComputedStyle("display", ["flex", "inline-flex"]);
  }

  /**
   * Check if the current node is a flex item.
   */
  get flexItem() {
    return this.isFlexItem(this.node);
  }

  /**
   * Check if the current node is a grid container i.e. a node that has a style
   * of `display:grid` or `display:inline-grid`.
   */
  get gridContainer() {
    return this.checkComputedStyle("display", ["grid", "inline-grid"]);
  }

  /**
   * Check if the current node is a grid item.
   */
  get gridItem() {
    return this.isGridItem(this.node);
  }

  /**
   * Check if the current node is a multi-column container, i.e. a node element whose
   * `column-width` or `column-count` property is not `auto`.
   */
  get multiColContainer() {
    const autoColumnWidth = this.checkComputedStyle("column-width", ["auto"]);
    const autoColumnCount = this.checkComputedStyle("column-count", ["auto"]);

    return !autoColumnWidth || !autoColumnCount;
  }

  /**
   * Check if the current node is a table row.
   */
  get tableRow() {
    return this.style && this.style.display === "table-row";
  }

  /**
   * Check if the current node is a row group.
   */
  get rowGroup() {
    return this.style && (
      this.style.display === "table-row-group" ||
      this.style.display === "table-header-group" ||
      this.style.display === "table-footer-group"
    );
  }

  /**
   * Check if the current node is a table column.
   */
  get tableColumn() {
    return this.style && this.style.display === "table-column";
  }

  /**
   * Check if the current node is a table column group.
   */
  get columnGroup() {
    return this.style && this.style.display === "table-column-group";
  }

  /**
   * Returns whether this element uses CSS layout.
   */
  get hasCssLayout() {
    return !this.isSvg && !this.isMathMl;
  }

  /**
   * Check if the current node is a non-replaced CSS inline box.
   */
  get nonReplacedInlineBox() {
    return (
      this.hasCssLayout &&
      this.nonReplaced &&
      this.style &&
      this.style.display === "inline"
    );
  }

  /**
   * Check if the current node is a non-replaced element. See `replaced()` for
   * a description of what a replaced element is.
   */
  get nonReplaced() {
    return !this.replaced;
  }

  /**
   * Check if the current node is an absolutely-positioned element.
   */
  get isAbsolutelyPositioned() {
    return this.checkComputedStyle("position", ["absolute", "fixed"]);
  }

  /**
   * Check if the current node is positioned (i.e. its position property has a value other
   * than static).
   */
  get isPositioned() {
    return this.checkComputedStyle("position", [
      "relative",
      "absolute",
      "fixed",
      "sticky",
    ]);
  }

  /**
   * Check if the current node is floated
   */
  get isFloated() {
    return this.style && this.style.cssFloat !== "none";
  }

  /**
   * Check if the current node is a replaced element i.e. an element with
   * content that will be replaced e.g. <img>, <audio>, <video> or <object>
   * elements.
   */
  get replaced() {
    // The <applet> element was removed in Gecko 56 so we can ignore them.
    // These are always treated as replaced elements:
    if (
      this.nodeNameOneOf([
        "audio",
        "br",
        "button",
        "canvas",
        "embed",
        "hr",
        "iframe",
        "input",
        "math",
        "object",
        "picture",
        "select",
        "svg",
        "textarea",
        "video",
      ])
    ) {
      return true;
    }

    // img tags are replaced elements only when the image has finished loading.
    if (this.nodeName === "img" && this.node.complete) {
      return true;
    }

    return false;
  }

  /**
   * Return the current node's nodeName.
   *
   * @returns {String}
   */
  get nodeName() {
    return this.node.nodeName ? this.node.nodeName.toLowerCase() : null;
  }

  /**
   * Return whether the node is a MathML element.
   */
  get isMathMl() {
    return this.node.namespaceURI === "http://www.w3.org/1998/Math/MathML";
  }

  /**
   * Return whether the node is an SVG element.
   */
  get isSvg() {
    return this.node.namespaceURI === "http://www.w3.org/2000/svg";
  }

  /**
   * Check if the current node's nodeName matches a value inside the value array.
   *
   * @param {Array} values
   *        Array of values to compare against.
   * @returns {Boolean}
   */
  nodeNameOneOf(values) {
    return values.includes(this.nodeName);
  }

  /**
   * Check if the current node is an absolutely-positioned grid element.
   * See: https://drafts.csswg.org/css-grid/#abspos-items
   *
   * @return {Boolean} whether or not the current node is absolutely-positioned by a
   *                   grid container.
   */
  isAbsPosGridElement() {
    if (!this.isAbsolutelyPositioned) {
      return false;
    }

    const containingBlock = this.getContainingBlock();

    return containingBlock !== null && this.isGridContainer(containingBlock);
  }

  /**
   * Check if a node is a flex item.
   *
   * @param {DOMNode} node
   *        The node to check.
   */
  isFlexItem(node) {
    return !!node.parentFlexElement;
  }

  /**
   * Check if a node is a flex container.
   *
   * @param {DOMNode} node
   *        The node to check.
   */
  isFlexContainer(node) {
    return !!node.getAsFlexContainer();
  }

  /**
   * Check if a node is a grid container.
   *
   * @param {DOMNode} node
   *        The node to check.
   */
  isGridContainer(node) {
    return node.getGridFragments().length > 0;
  }

  /**
   * Check if a node is a grid item.
   *
   * @param {DOMNode} node
   *        The node to check.
   */
  isGridItem(node) {
    return !!this.getParentGridElement(this.node);
  }

  isVisitedRule() {
    if (!CssLogic.hasVisitedState(this.node)) {
      return false;
    }

    const selectors = CssLogic.getSelectors(this.cssRule);
    if (!selectors.some(s => s.endsWith(":visited"))) {
      return false;
    }

    const { bindingElement, pseudo } = CssLogic.getBindingElementAndPseudo(
      this.node
    );

    for (let i = 0; i < selectors.length; i++) {
      if (
        !selectors[i].endsWith(":visited") &&
        InspectorUtils.selectorMatchesElement(
          bindingElement,
          this.cssRule,
          i,
          pseudo,
          true
        )
      ) {
        // Match non :visited selector.
        return false;
      }
    }

    return true;
  }

  /**
   * Return the current node's ancestor that generates its containing block.
   */
  getContainingBlock() {
    return this.node ? InspectorUtils.containingBlockOf(this.node) : null;
  }

  getParentGridElement(node) {
    // The documentElement can't be a grid item, only a container, so bail out.
    if (node.flattenedTreeParentNode === node.ownerDocument) {
      return null;
    }

    if (node.nodeType === node.ELEMENT_NODE) {
      const display = this.style ? this.style.display : null;

      if (!display || display === "none" || display === "contents") {
        // Doesn't generate a box, not a grid item.
        return null;
      }
      if (this.isAbsolutelyPositioned || this.isFloated) {
        // Out of flow, not a grid item.
        return null;
      }
    } else if (node.nodeType !== node.TEXT_NODE) {
      return null;
    }

    for (
      let p = node.flattenedTreeParentNode;
      p;
      p = p.flattenedTreeParentNode
    ) {
      if (this.isGridContainer(p)) {
        // It's a grid item!
        return p;
      }

      const style = node.ownerGlobal.getComputedStyle(p);
      const display = style.display;

      if (display !== "contents") {
        return null; // Not a grid item, for sure.
      }
      // display: contents, walk to the parent
    }
    return null;
  }
}

exports.inactivePropertyHelper = new InactivePropertyHelper();

/**
 * Returns all CSS property names except given properties.
 *
 * @param {Array} - propertiesToIgnore
 *        Array of property ignored.
 * @return {Array}
 *        Array of all CSS property name except propertiesToIgnore.
 */
function allCssPropertiesExcept(propertiesToIgnore) {
  const properties = new Set(
    InspectorUtils.getCSSPropertyNames({ includeAliases: true })
  );

  for (const name of propertiesToIgnore) {
    properties.delete(name);
  }

  return [...properties];
}
