/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

[ChromeOnly,
 Exposed=Window]
interface TreeContentView
{
  /**
   * Retrieve the content item associated with the specified row.
   */
  [Throws]
  Element? getItemAtIndex(long row);

  /**
   * Retrieve the index associated with the specified content item.
   */
  long getIndexOfItem(Element? item);
};
TreeContentView includes TreeView;
