/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CreateElementTransaction_h
#define CreateElementTransaction_h

#include "mozilla/EditorDOMPoint.h"
#include "mozilla/EditTransactionBase.h"
#include "mozilla/RefPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupportsImpl.h"

class nsAtom;

/**
 * A transaction that creates a new node in the content tree.
 */
namespace mozilla {
namespace dom {
class Element;
}

class EditorBase;

class CreateElementTransaction final : public EditTransactionBase {
 protected:
  template <typename PT, typename CT>
  CreateElementTransaction(EditorBase& aEditorBase, nsAtom& aTag,
                           const EditorDOMPointBase<PT, CT>& aPointToInsert);

 public:
  /**
   * Create a transaction for creating a new child node of the container of
   * aPointToInsert of type aTag.
   *
   * @param aEditorBase     The editor which manages the transaction.
   * @param aTag            The tag (P, HR, TABLE, etc.) for the new element.
   * @param aPointToInsert  The new node will be inserted before the child at
   *                        aPointToInsert.  If this refers end of the container
   *                        or after, the new node will be appended to the
   *                        container.
   */
  template <typename PT, typename CT>
  static already_AddRefed<CreateElementTransaction> Create(
      EditorBase& aEditorBase, nsAtom& aTag,
      const EditorDOMPointBase<PT, CT>& aPointToInsert);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(CreateElementTransaction,
                                           EditTransactionBase)

  NS_DECL_EDITTRANSACTIONBASE
  NS_DECL_EDITTRANSACTIONBASE_GETASMETHODS_OVERRIDE(CreateElementTransaction)

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD RedoTransaction() override;

  dom::Element* GetNewElement() const { return mNewElement; }

 protected:
  virtual ~CreateElementTransaction() = default;

  /**
   * InsertNewNode() inserts mNewNode before the child node at mPointToInsert.
   */
  MOZ_CAN_RUN_SCRIPT void InsertNewNode(ErrorResult& aError);

  // The document into which the new node will be inserted.
  RefPtr<EditorBase> mEditorBase;

  // The tag (mapping to object type) for the new element.
  RefPtr<nsAtom> mTag;

  // The DOM point we will insert mNewNode.
  EditorDOMPoint mPointToInsert;

  // The new node to insert.
  RefPtr<dom::Element> mNewElement;
};

}  // namespace mozilla

#endif  // #ifndef CreateElementTransaction_h
