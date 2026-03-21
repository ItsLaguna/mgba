/* Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QButtonGroup>
#include <QPushButton>
#include <QWidget>
#include <QString>

class QLabel;
class QLayout;

namespace QGBA {

struct LibraryFilter {
	enum class Section { All = 0, RecentlyPlayed, Favorites } section = Section::All;
	QString platform;    // empty = any; "GBA", "GBC", "GB", "SGB"
	QString searchTerm;

	bool operator==(const LibraryFilter& o) const {
		return section == o.section && platform == o.platform && searchTerm == o.searchTerm;
	}
};

class LibrarySideBar : public QWidget {
Q_OBJECT

public:
	explicit LibrarySideBar(QWidget* parent = nullptr);

signals:
	void filterChanged(const QGBA::LibraryFilter& filter);

public slots:
	// Called by LibraryController after game counts are known
	void setGameCounts(int all, int gba, int gbc, int gb, int sgb);
	// Deselect all platform buttons (e.g. when "All Games" is clicked)
	void clearPlatformSelection();

private:
	void addSectionHeader(QLayout* layout, const QString& text);
	void emitFilter();

	QButtonGroup* m_sectionGroup  = nullptr;
	QButtonGroup* m_platformGroup = nullptr;

	QLabel* m_countAll = nullptr;
	QLabel* m_countGBA = nullptr;
	QLabel* m_countGBC = nullptr;
	QLabel* m_countGB  = nullptr;
	QLabel* m_countSGB = nullptr;

	LibraryFilter m_current;
};

} // namespace QGBA
