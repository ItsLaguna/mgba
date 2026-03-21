/* Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "LibraryGridDelegate.h"
#include "LibraryCoverManager.h"
#include "LibraryModel.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionViewItem>
#include <QModelIndex>
#include <QFontMetrics>

using namespace QGBA;

// Platform badge colours (bg, text)
static struct { const char* platform; QColor bg; QColor fg; } kPlatformColours[] = {
	// Colors match mGBA's platform icon palette
	{ "GBA",  QColor(0x5c, 0x3d, 0x9b), QColor(Qt::white) }, // indigo/violet — mGBA GBA brand color
	{ "GBC",  QColor(0x37, 0x8a, 0xdd), QColor(Qt::white) }, // blue
	{ "GB",   QColor(0x70, 0x70, 0x70), QColor(Qt::white) }, // gray
	{ "SGB",  QColor(0x99, 0x55, 0x00), QColor(Qt::white) }, // amber/brown
};

static QColor platformBg(const QString& plat) {
	for (auto& c : kPlatformColours) {
		if (plat == c.platform) return c.bg;
	}
	return QColor(0x44, 0x44, 0x44);
}

LibraryGridDelegate::LibraryGridDelegate(LibraryCoverManager* covers, QObject* parent)
	: QStyledItemDelegate(parent)
	, m_covers(covers)
{}

QSize LibraryGridDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
	return cardSize();
}

QSize LibraryGridDelegate::cardSize() const {
	// Grid cell size: card + 8px gap on each side for breathing room
	// setGridSize with spacing(0) makes Qt distribute the extra space evenly = centered
	return QSize(m_coverSize + 24, m_coverSize + 52);
}

void LibraryGridDelegate::paint(QPainter* painter,
                                const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setRenderHint(QPainter::SmoothPixmapTransform);

	// Card rect: 4px margin inside the cell
	const QRect r = option.rect.adjusted(4, 4, -4, -4);
	const bool selected = option.state & QStyle::State_Selected;
	const bool hovered  = option.state & QStyle::State_MouseOver;

	// ---- Card background (rounded, clips everything) ----------------------
	QPainterPath cardPath;
	cardPath.addRoundedRect(r, CORNER, CORNER);
	painter->setClipPath(cardPath); // clip ALL drawing to card shape

	QColor cardBg = option.palette.color(QPalette::Base);
	if (selected) {
		cardBg = option.palette.color(QPalette::Highlight).darker(130);
	} else if (hovered) {
		cardBg = option.palette.color(QPalette::AlternateBase);
	}
	painter->fillPath(cardPath, cardBg);

	// ---- Art area ---------------------------------------------------------
	const QRect artRect(r.left(), r.top(), r.width(), r.height() - 40);

	// Retrieve cover
	const QString title    = index.data(Qt::DisplayRole).toString();
	const QString fullpath = index.data(LibraryModel::FullPathRole).toString();
	// We need platform and internal code - stored in sibling columns
	const QString platform = index.sibling(index.row(), LibraryModel::COL_PLATFORM).data().toString();
	// internalCode isn't directly in the model - use filename stem as fallback
	const QString filename = fullpath.section('/', -1);
	const QString stem     = filename.section('.', 0, -2);

	QPixmap cover;
	if (m_covers) {
		// Try internalCode empty for now (model doesn't expose it yet), title + filename
		cover = m_covers->cover(QString(), title, filename);
	}

	if (!cover.isNull()) {
		// Scale-to-fill the art rect
		QPixmap scaled = cover.scaled(artRect.size(),
		                              Qt::KeepAspectRatioByExpanding,
		                              Qt::SmoothTransformation);
		QRect src(0, 0, artRect.width(), artRect.height());
		int ox = (scaled.width()  - artRect.width())  / 2;
		int oy = (scaled.height() - artRect.height()) / 2;
		painter->drawPixmap(artRect, scaled, QRect(ox, oy, artRect.width(), artRect.height()));
	} else {
		// Placeholder — coloured gradient with platform initial
		QColor bg = platformBg(platform);
		painter->fillRect(artRect, bg.darker(120));

		// Platform badge centred
		QFont badgeFont = painter->font();
		badgeFont.setPixelSize(32);
		badgeFont.setBold(true);
		painter->setFont(badgeFont);
		painter->setPen(QColor(255, 255, 255, 60));
		painter->drawText(artRect, Qt::AlignCenter, platform.isEmpty() ? "?" : platform);
	}
	// Platform badge pill (bottom-left of art)
	if (!platform.isEmpty()) {
		QFont pf = painter->font();
		pf.setPixelSize(9);
		pf.setBold(true);
		painter->setFont(pf);
		QFontMetrics pfm(pf);
		int pw = pfm.horizontalAdvance(platform) + 10;
		int ph = 16;
		QRect badgeRect(artRect.left() + 6, artRect.bottom() - ph - 6, pw, ph);
		QPainterPath bp;
		bp.addRoundedRect(badgeRect, 4, 4);
		painter->fillPath(bp, platformBg(platform));
		painter->setPen(Qt::white);
		painter->drawText(badgeRect, Qt::AlignCenter, platform);
	}

	// ---- Title row --------------------------------------------------------
	const QRect titleRect(r.left(), r.top() + r.height() - 40, r.width(), 40);

	QFont tf = option.font;
	tf.setPixelSize(11);
	painter->setFont(tf);
	painter->setPen(selected
		? option.palette.color(QPalette::HighlightedText)
		: option.palette.color(QPalette::Text));

	QFontMetrics tfm(tf);
	QString elidedTitle = tfm.elidedText(title, Qt::ElideRight, titleRect.width() - 8);
	painter->drawText(titleRect.adjusted(4, 4, -4, -4),
	                  Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
	                  elidedTitle);

	// ---- Selection border (drawn last, outside clip) ---------------------
	painter->setClipping(false);
	if (selected) {
		painter->setPen(QPen(option.palette.color(QPalette::Highlight), 2));
		painter->setBrush(Qt::NoBrush);
		painter->drawRoundedRect(r.adjusted(1, 1, -1, -1), CORNER, CORNER);
	}

	painter->restore();
}
