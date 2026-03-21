/* Copyright (c) 2014-2017 waddlesplash
 * Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once
#include <QHash>
#include <QListWidget>
#include <QObject>
#include "LibraryEntry.h"
#include "LibraryController.h"
namespace QGBA {
// LibraryGrid is not part of a class hierarchy (AbstractGameList no longer
// exists in this codebase). It is owned directly by LibraryController.
class LibraryGrid : public QObject {
Q_OBJECT
public:
	explicit LibraryGrid(LibraryController* parent = nullptr);
	~LibraryGrid();
	QString selectedEntry();
	void selectEntry(const QString& fullpath);
	void setViewStyle(LibraryStyle newStyle);
	void resetEntries(const QList<LibraryEntry>& items);
	void addEntries(const QList<LibraryEntry>& items);
	void updateEntries(const QList<LibraryEntry>& items);
	void removeEntries(const QList<QString>& items);
	void addEntry(const LibraryEntry& item);
	void updateEntry(const LibraryEntry& item);
	void removeEntry(const QString& item);
	void setShowFilename(bool show);
	QWidget* widget() { return m_widget; }
signals:
	void startGame();
private:
	QListWidget* m_widget;
	const quint32 GRID_BANNER_WIDTH  = 320;
	const quint32 GRID_BANNER_HEIGHT = 240;
	const quint32 ICON_BANNER_WIDTH  = 64;
	const quint32 ICON_BANNER_HEIGHT = 64;
	QHash<QString, QListWidgetItem*> m_items;
	LibraryStyle m_currentStyle;
	bool m_showFilename = false;
};
}
