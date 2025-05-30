/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

[ChromeOnly, OverrideBuiltins,
 Exposed=Window]
interface ImageDocument : HTMLDocument {
  /* Whether the image is overflowing visible area. */
  readonly attribute boolean imageIsOverflowing;

  /* Whether the image has been resized to fit visible area. */
  readonly attribute boolean imageIsResized;

  /* Resize the image to fit visible area. */
  void shrinkToFit();

  /* Restore image original size. */
  void restoreImage();

  /* Restore the image, trying to keep a certain pixel in the same position.
   * The coordinate system is that of the shrunken image.
   */
  void restoreImageTo(long x, long y);
};
