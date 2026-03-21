/* Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "LibrarySideBar.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

using namespace QGBA;

namespace {

static const QString kSidebarStyle = QStringLiteral(R"(
	QWidget#LibrarySideBarInner {
		background: palette(base);
	}
	QPushButton[sidebarItem="true"] {
		text-align: left;
		padding: 0 10px 0 12px;
		border: none;
		border-left: 2px solid transparent;
		border-radius: 0;
		color: palette(text);
		font-size: 12px;
		height: 28px;
		background: transparent;
	}
	QPushButton[sidebarItem="true"]:hover {
		background: palette(alternateBase);
		color: palette(text);
	}
	QPushButton[sidebarItem="true"]:checked {
		background-color: rgba(106, 176, 76, 40);
		border-left: 2px solid #6ab04c;
		color: #6ab04c;
		font-weight: bold;
	}
	QLabel#sidebarHeader {
		color: palette(placeholderText);
		font-size: 10px;
		font-weight: bold;
		padding: 8px 10px 2px 10px;
		letter-spacing: 1px;
	}
	QLabel#countBadge {
		color: palette(text);
		font-size: 10px;
		background: palette(alternateBase);
		border-radius: 7px;
		padding: 0 5px;
		min-width: 14px;
	}
)");

} // namespace

LibrarySideBar::LibrarySideBar(QWidget* parent)
	: QWidget(parent)
{
	setFixedWidth(190);
	setObjectName("LibrarySideBar");
	setStyleSheet(QStringLiteral(
		"#LibrarySideBar { background: palette(base); border-right: 1px solid palette(mid); }"
	));

	auto* scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	auto* inner = new QWidget;
	inner->setObjectName("LibrarySideBarInner");
	inner->setStyleSheet(kSidebarStyle);

	auto* layout = new QVBoxLayout(inner);
	layout->setContentsMargins(0, 4, 0, 8);
	layout->setSpacing(0);

	// ---- Library section --------------------------------------------------
	addSectionHeader(layout, tr("LIBRARY"));

	m_sectionGroup = new QButtonGroup(this);
	m_sectionGroup->setExclusive(true);

	// "All Games" with count badge via a row widget
	{
		auto* btn = new QPushButton(tr("All Games"), inner);
		btn->setProperty("sidebarItem", true);
		btn->setCheckable(true);
		btn->setChecked(true);
		btn->setFlat(true);
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_sectionGroup->addButton(btn, static_cast<int>(LibraryFilter::Section::All));

		m_countAll = new QLabel("0", inner);
		m_countAll->setObjectName("countBadge");
		m_countAll->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);

		auto* row = new QWidget(inner);
		auto* hl  = new QHBoxLayout(row);
		hl->setContentsMargins(0, 0, 6, 0);
		hl->setSpacing(4);
		hl->addWidget(btn);
		hl->addWidget(m_countAll);
		layout->addWidget(row);
	}

	{
		auto* btn = new QPushButton(tr("Recently Played"), inner);
		btn->setProperty("sidebarItem", true);
		btn->setCheckable(true);
		btn->setFlat(true);
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_sectionGroup->addButton(btn, static_cast<int>(LibraryFilter::Section::RecentlyPlayed));
		layout->addWidget(btn);
	}

	{
		auto* btn = new QPushButton(tr("Favorites"), inner);
		btn->setProperty("sidebarItem", true);
		btn->setCheckable(true);
		btn->setFlat(true);
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_sectionGroup->addButton(btn, static_cast<int>(LibraryFilter::Section::Favorites));
		layout->addWidget(btn);
	}

	// ---- Separator --------------------------------------------------------
	auto* sep1 = new QFrame(inner);
	sep1->setFrameShape(QFrame::HLine);
	sep1->setFrameShadow(QFrame::Sunken);
	layout->addWidget(sep1);

	// ---- Platform section -------------------------------------------------
	addSectionHeader(layout, tr("PLATFORM"));

	m_platformGroup = new QButtonGroup(this);
	m_platformGroup->setExclusive(false);

	auto addPlatformBtn = [&](int id, const QString& label, QLabel*& countOut) {
		auto* btn = new QPushButton(label, inner);
		btn->setProperty("sidebarItem", true);
		btn->setCheckable(true);
		btn->setFlat(true);
		btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_platformGroup->addButton(btn, id);

		countOut = new QLabel("0", inner);
		countOut->setObjectName("countBadge");
		countOut->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);

		auto* row = new QWidget(inner);
		auto* hl  = new QHBoxLayout(row);
		hl->setContentsMargins(0, 0, 6, 0);
		hl->setSpacing(4);
		hl->addWidget(btn);
		hl->addWidget(countOut);
		layout->addWidget(row);
	};

	addPlatformBtn(0, tr("Game Boy Advance"), m_countGBA);
	addPlatformBtn(1, tr("Game Boy Color"),   m_countGBC);
	addPlatformBtn(2, tr("Game Boy"),         m_countGB);
	addPlatformBtn(3, tr("Super Game Boy"),   m_countSGB);

	layout->addStretch();
	scroll->setWidget(inner);

	auto* outerLayout = new QVBoxLayout(this);
	outerLayout->setContentsMargins(0, 0, 0, 0);
	outerLayout->setSpacing(0);
	outerLayout->addWidget(scroll);

	// Connections
	connect(m_sectionGroup, QOverload<int>::of(&QButtonGroup::idClicked), this, [this](int id) {
		m_current.section = static_cast<LibraryFilter::Section>(id);
		// Clicking any section clears the platform filter
		m_current.platform = QString();
		for (auto* b : m_platformGroup->buttons()) {
			b->setChecked(false);
		}
		emitFilter();
	});

	connect(m_platformGroup, QOverload<int>::of(&QButtonGroup::idClicked), this, [this](int id) {
		static const QStringList platforms = {"GBA", "GBC", "GB", "SGB"};
		auto* clicked = m_platformGroup->button(id);
		// quasi-exclusive within platform group
		for (auto* b : m_platformGroup->buttons()) {
			if (b != clicked) b->setChecked(false);
		}
		m_current.platform = clicked->isChecked() ? platforms.value(id) : QString();
		// Selecting a platform resets section to All so the filter makes sense
		if (!m_current.platform.isEmpty()) {
			m_current.section = LibraryFilter::Section::All;
			// Visually deselect section buttons - re-check "All Games"
			if (auto* allBtn = m_sectionGroup->button(0)) {
				allBtn->setChecked(true);
			}
		}
		emitFilter();
	});
}

void LibrarySideBar::setGameCounts(int all, int gba, int gbc, int gb, int sgb) {
	if (m_countAll) m_countAll->setText(QString::number(all));
	if (m_countGBA) m_countGBA->setText(QString::number(gba));
	if (m_countGBC) m_countGBC->setText(QString::number(gbc));
	if (m_countGB)  m_countGB->setText(QString::number(gb));
	if (m_countSGB) m_countSGB->setText(QString::number(sgb));
}

void LibrarySideBar::addSectionHeader(QLayout* layout, const QString& text) {
	auto* label = new QLabel(text);
	label->setObjectName("sidebarHeader");
	layout->addWidget(label);
}

void LibrarySideBar::clearPlatformSelection() {
	for (auto* b : m_platformGroup->buttons()) {
		b->setChecked(false);
	}
	m_current.platform = QString();
}

void LibrarySideBar::emitFilter() {
	emit filterChanged(m_current);
}
