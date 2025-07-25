/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

interface MozTreeView;

dictionary TreeCellInfo {
    long row = 0;
    TreeColumn? col = null;
    DOMString childElt = "";
};

[ChromeOnly,
 Exposed=Window]
interface XULTreeElement : XULElement
{
  [HTMLConstructor] constructor();

  /**
   * Obtain the columns.
   */
  readonly attribute TreeColumns? columns;

  /**
   * The view that backs the tree and that supplies it with its data.
   * It is dynamically settable, either using a view attribute on the
   * tree tag or by setting this attribute to a new value.
   */
  [SetterThrows, NeedsCallerType]
  attribute MozTreeView? view;

  /**
   * Whether or not we are currently focused.
   */
  attribute boolean focused;

  /**
   * Obtain the treebody content node
   */
  readonly attribute Element? treeBody;

  /**
   * Obtain the height of a row.
   */
  readonly attribute long rowHeight;

  /**
   * Obtain the width of a row.
   */
  readonly attribute long rowWidth;

  /**
   * Get the pixel position of the horizontal scrollbar.
   */
  readonly attribute long horizontalPosition;

  /**
   * Get the index of the first visible row.
   */
  long getFirstVisibleRow();

  /**
   * Get the index of the last visible row.
   */
  long getLastVisibleRow();

  /**
   * Gets the number of possible visible rows.
   */
  long getPageLength();

  /**
   * Ensures that a row at a given index is visible.
   */
  void ensureRowIsVisible(long index);

  /**
   * Ensures that a given cell in the tree is visible.
   */
  [Throws]
  void ensureCellIsVisible(long row, TreeColumn? col);

  /**
   * Scrolls such that the row at index is at the top of the visible view.
   */
  void scrollToRow(long index);

  /**
   * Scroll the tree up or down by numLines lines. Positive
   * values move down in the tree. Prevents scrolling off the
   * end of the tree.
   */
  void scrollByLines(long numLines);

  /**
   * Scroll the tree up or down by numPages pages. A page
   * is considered to be the amount displayed by the tree.
   * Positive values move down in the tree. Prevents scrolling
   * off the end of the tree.
   */
  void scrollByPages(long numPages);

  /**
   * Invalidation methods for fine-grained painting control.
   */
  void invalidate();
  void invalidateColumn(TreeColumn? col);
  void invalidateRow(long index);
  void invalidateCell(long row, TreeColumn? col);
  void invalidateRange(long startIndex, long endIndex);

  /**
   * A hit test that can tell you what row the mouse is over.
   * returns -1 for invalid mouse coordinates.
   *
   * The coordinate system is the client coordinate system for the
   * document this tree lives in, and the units are CSS pixels.
   */
  long getRowAt(long x, long y);

  /**
   * A hit test that can tell you what cell the mouse is over.
   * TreeCellInfo.row is the row index hit,  returns -1 for invalid mouse
   * coordinates.  TreeCellInfo.col is the column hit.
   * TreeCellInfo.childElt is the pseudoelement hit: this can have values of
   * "cell", "twisty", "image", and "text".
   *
   * The coordinate system is the client coordinate system for the
   * document this tree lives in, and the units are CSS pixels.
   */
  [Throws]
  TreeCellInfo getCellAt(long x, long y);

  /**
   * Find the coordinates of an element within a specific cell.
   */
  [Throws]
  DOMRect? getCoordsForCellItem(long row, TreeColumn col, DOMString element);

  /**
   * Determine if the text of a cell is being cropped or not.
   */
  [Throws]
  boolean isCellCropped(long row, TreeColumn? col);

  /**
   * The view is responsible for calling these notification methods when
   * rows are added or removed.  Index is the position at which the new
   * rows were added or at which rows were removed.  For
   * non-contiguous additions/removals, this method should be called multiple times.
   */
  void rowCountChanged(long index, long count);

  /**
   * Notify the tree that the view is about to perform a batch
   * update, that is, add, remove or invalidate several rows at once.
   * This must be followed by calling endUpdateBatch(), otherwise the tree
   * will get out of sync.
   */
  void beginUpdateBatch();

  /**
   * Notify the tree that the view has completed a batch update.
   */
  void endUpdateBatch();

  /**
   * Called on a theme switch to flush out the tree's style and image caches.
   */
  void clearStyleAndImageCaches();

  /**
   * Remove an image source from the image cache to allow its invalidation.
   */
  [Throws]
  void removeImageCacheEntry(long row, TreeColumn col);
};
