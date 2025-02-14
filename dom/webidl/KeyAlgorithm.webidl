/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://www.w3.org/TR/WebCryptoAPI/
 */

dictionary KeyAlgorithm {
  required DOMString name;
};

[GenerateConversionToJS]
dictionary AesKeyAlgorithm : KeyAlgorithm {
  required unsigned short length;
};

[GenerateConversionToJS]
dictionary EcKeyAlgorithm : KeyAlgorithm {
  required DOMString namedCurve;
};

[GenerateConversionToJS]
dictionary HmacKeyAlgorithm : KeyAlgorithm {
  required KeyAlgorithm hash;
  required unsigned long length;
};

[GenerateConversionToJS]
dictionary RsaHashedKeyAlgorithm : KeyAlgorithm {
  required unsigned short modulusLength;
  required Uint8Array publicExponent;
  required KeyAlgorithm hash;
};

[GenerateConversionToJS]
dictionary DhKeyAlgorithm : KeyAlgorithm {
  required Uint8Array prime;
  required Uint8Array generator;
};

