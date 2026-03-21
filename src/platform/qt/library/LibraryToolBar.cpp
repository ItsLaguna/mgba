/* Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "LibraryToolBar.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QPainter>
#include <QToolButton>

using namespace QGBA;

static const QString kToolBarStyle = QStringLiteral(R"(
	QWidget#LibraryToolBar {
		background: palette(window);
		border-bottom: 1px solid palette(mid);
	}
	QToolButton {
		border: none;
		border-radius: 4px;
		padding: 3px 5px;
		color: palette(button-text);
		background: transparent;
		font-size: 14px;
	}
	QToolButton:hover { background: palette(midlight); }
	QToolButton:checked {
		background: palette(highlight);
		color: palette(highlighted-text);
	}
	QComboBox {
		border: 1px solid palette(mid);
		border-radius: 4px;
		padding: 2px 6px;
		background: palette(button);
		color: palette(button-text);
		min-width: 80px;
	}
	QComboBox:hover { border-color: palette(shadow); }
	QComboBox::drop-down { border: none; width: 16px; }
	QComboBox::down-arrow {
		image: none;
		border-left: 4px solid transparent;
		border-right: 4px solid transparent;
		border-top: 5px solid palette(button-text);
		width: 0; height: 0;
		margin-right: 4px;
	}
	QLineEdit {
		border: 1px solid palette(mid);
		border-radius: 4px;
		padding: 2px 6px;
		background: palette(base);
		color: palette(text);
		min-width: 0;
		max-width: 200px;
	}
	QLineEdit:focus { border-color: palette(highlight); }
	QFrame#tbSep {
		color: palette(mid);
		max-width: 1px;
		min-width: 1px;
		margin: 5px 4px;
	}
	QSlider::groove:horizontal {
		height: 4px;
		background: palette(mid);
		border-radius: 2px;
	}
	QSlider::handle:horizontal {
		width: 12px; height: 12px;
		margin: -4px 0;
		border-radius: 6px;
		background: palette(highlight);
	}
	QSlider::sub-page:horizontal {
		background: palette(highlight);
		border-radius: 2px;
	}
)");

static QFrame* makeSep() {
	auto* f = new QFrame;
	f->setObjectName("tbSep");
	f->setFrameShape(QFrame::VLine);
	f->setFrameShadow(QFrame::Plain);
	return f;
}

LibraryToolBar::LibraryToolBar(QWidget* parent)
	: QWidget(parent)
{
	setObjectName("LibraryToolBar");
	setStyleSheet(kToolBarStyle);
	setFixedHeight(38);

	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(6, 0, 6, 0);
	layout->setSpacing(2);

	// ---- View buttons ----------------------------------------------------
	m_viewGroup = new QButtonGroup(this);
	m_viewGroup->setExclusive(true);

	// Paint view mode icons as pixmaps so they render at consistent size
	auto makeIcon = [](auto paintFn) -> QIcon {
		QPixmap px(16, 16);
		px.fill(Qt::transparent);
		QPainter p(&px);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		paintFn(p);
		p.end();
		return QIcon(px);
	};

	// List view: three horizontal lines
	QIcon iconList = makeIcon([](QPainter& p) {
		p.setBrush(QColor(180, 180, 180));
		p.drawRoundedRect(0, 2, 16, 3, 1, 1);
		p.drawRoundedRect(0, 7, 16, 3, 1, 1);
		p.drawRoundedRect(0, 12, 16, 3, 1, 1);
	});

	// Tree view: indent hierarchy lines
	QIcon iconTree = makeIcon([](QPainter& p) {
		p.setBrush(QColor(180, 180, 180));
		p.drawRoundedRect(0, 2, 16, 3, 1, 1);
		p.drawRoundedRect(3, 7, 13, 3, 1, 1);
		p.drawRoundedRect(6, 12, 10, 3, 1, 1);
	});

	// Grid view: 2x2 block of squares
	QIcon iconGrid = makeIcon([](QPainter& p) {
		p.setBrush(QColor(180, 180, 180));
		p.drawRoundedRect(0, 0, 7, 7, 1, 1);
		p.drawRoundedRect(9, 0, 7, 7, 1, 1);
		p.drawRoundedRect(0, 9, 7, 7, 1, 1);
		p.drawRoundedRect(9, 9, 7, 7, 1, 1);
	});

	auto makeBtn = [](const QIcon& icon, const QString& tip) {
		auto* btn = new QToolButton;
		btn->setIcon(icon);
		btn->setIconSize(QSize(16, 16));
		btn->setToolTip(tip);
		btn->setCheckable(true);
		btn->setAutoExclusive(false);
		btn->setFixedSize(28, 26);
		return btn;
	};

	auto* btnList = makeBtn(iconList, tr("List view"));
	auto* btnTree = makeBtn(iconTree, tr("Tree view"));
	auto* btnGrid = makeBtn(iconGrid, tr("Grid view"));
	btnList->setChecked(true);

	m_viewGroup->addButton(btnList, 0);
	m_viewGroup->addButton(btnTree, 1);
	m_viewGroup->addButton(btnGrid, 2);

	layout->addWidget(btnList);
	layout->addWidget(btnTree);
	layout->addWidget(btnGrid);

	// ---- Cover icon + slider (right after grid button, hidden unless grid) --
	m_sliderWrap = new QWidget(this);
	m_sliderWrap->setVisible(false);
	auto* sl = new QHBoxLayout(m_sliderWrap);
	sl->setContentsMargins(4, 0, 0, 0);
	sl->setSpacing(4);

	m_coverSlider = new QSlider(Qt::Horizontal, m_sliderWrap);
	m_coverSlider->setRange(80, 280);
	m_coverSlider->setValue(160);
	m_coverSlider->setFixedWidth(120);
	m_coverSlider->setToolTip(tr("Cover size"));

	sl->addWidget(m_coverSlider);
	layout->addWidget(m_sliderWrap);

	connect(m_coverSlider, &QSlider::valueChanged, this, &LibraryToolBar::coverSizeChanged);

	// ---- Separator -------------------------------------------------------
	layout->addWidget(makeSep());

	// ---- Right side: filters + search, pushed right ----------------------
	layout->addStretch(1);

	// Section filter
	m_sectionBox = new QComboBox(this);
	m_sectionBox->setToolTip(tr("Filter by section"));
	m_sectionBox->addItem(tr("All Games"),       static_cast<int>(LibraryFilter::Section::All));
	m_sectionBox->addItem(tr("Recently Played"), static_cast<int>(LibraryFilter::Section::RecentlyPlayed));
	m_sectionBox->addItem(tr("Favorites"),       static_cast<int>(LibraryFilter::Section::Favorites));
	layout->addWidget(m_sectionBox);

	layout->addSpacing(4);

	// Platform filter
	m_platformBox = new QComboBox(this);
	m_platformBox->setToolTip(tr("Filter by platform"));
	m_platformBox->addItem(tr("All Platforms"),    QString());
	m_platformBox->addItem(tr("Game Boy Advance"), QString("GBA"));
	m_platformBox->addItem(tr("Game Boy Color"),   QString("GBC"));
	m_platformBox->addItem(tr("Game Boy"),         QString("GB"));
	m_platformBox->addItem(tr("Super Game Boy"),   QString("SGB"));
	layout->addWidget(m_platformBox);

	layout->addSpacing(4);

	// Search bar — fixed width, not stretchy
	m_searchBar = new QLineEdit(this);
	m_searchBar->setPlaceholderText(tr("Search..."));
	m_searchBar->setClearButtonEnabled(true);
	m_searchBar->setFixedWidth(180);
	layout->addWidget(m_searchBar);

	// ---- Connections -----------------------------------------------------
	connect(m_viewGroup, QOverload<int>::of(&QButtonGroup::idClicked),
	        this, [this](int id) {
		m_sliderWrap->setVisible(id == 2);
		emit viewModeChanged(id);
	});

	connect(m_sectionBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, [this](int idx) {
		m_current.section = static_cast<LibraryFilter::Section>(
			m_sectionBox->itemData(idx).toInt());
		m_current.platform = QString();
		m_platformBox->blockSignals(true);
		m_platformBox->setCurrentIndex(0);
		m_platformBox->blockSignals(false);
		emitFilter();
	});

	connect(m_platformBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, [this](int) {
		m_current.platform = m_platformBox->currentData().toString();
		emitFilter();
	});

	connect(m_searchBar, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_current.searchTerm = text;
		emitFilter();
	});
}

void LibraryToolBar::setViewMode(int mode) {
	if (auto* btn = m_viewGroup->button(mode)) {
		btn->setChecked(true);
	}
	m_sliderWrap->setVisible(mode == 2);
}

void LibraryToolBar::setCoverSize(int size) {
	if (m_coverSlider) {
		m_coverSlider->blockSignals(true);
		m_coverSlider->setValue(size);
		m_coverSlider->blockSignals(false);
	}
}

void LibraryToolBar::setGameCounts(int all, int gba, int gbc, int gb, int sgb) {
	m_platformBox->setItemText(0, tr("All Platforms (%1)").arg(all));
	m_platformBox->setItemText(1, tr("Game Boy Advance (%1)").arg(gba));
	m_platformBox->setItemText(2, tr("Game Boy Color (%1)").arg(gbc));
	m_platformBox->setItemText(3, tr("Game Boy (%1)").arg(gb));
	m_platformBox->setItemText(4, tr("Super Game Boy (%1)").arg(sgb));
}

int LibraryToolBar::coverSize() const {
	return m_coverSlider ? m_coverSlider->value() : 160;
}

void LibraryToolBar::emitFilter() {
	emit filterChanged(m_current);
}
