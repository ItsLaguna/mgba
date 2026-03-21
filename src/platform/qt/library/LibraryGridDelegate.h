/* Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QStyledItemDelegate>

namespace QGBA {

class LibraryCoverManager;

// Paints DuckStation-style cover cards in a QListView (icon mode).
// Each card shows:
//   - Cover image if available, else a coloured placeholder with platform badge
//   - Game title below the art
//   - Selection/hover highlight
class LibraryGridDelegate : public QStyledItemDelegate {
Q_OBJECT

public:
	static constexpr int CARD_WIDTH   = 180;
	static constexpr int CARD_HEIGHT  = 220; // art area + title row
	static constexpr int ART_HEIGHT   = 160;
	static constexpr int CORNER       = 6;

	explicit LibraryGridDelegate(LibraryCoverManager* covers, QObject* parent = nullptr);

	void  setCoverSize(int size) { m_coverSize = size; }
	int   coverSize() const { return m_coverSize; }
	QSize cardSize() const;  // returns the full card size for QListView::setGridSize

	void paint(QPainter* painter,
	           const QStyleOptionViewItem& option,
	           const QModelIndex& index) const override;

	QSize sizeHint(const QStyleOptionViewItem& option,
	               const QModelIndex& index) const override;

private:
	LibraryCoverManager* m_covers;
	int m_coverSize = 160;
};

} // namespace QGBA
