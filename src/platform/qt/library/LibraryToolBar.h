/* Copyright (c) 2013-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <QWidget>
#include "LibrarySideBar.h" // for LibraryFilter

class QButtonGroup;
class QComboBox;
class QLineEdit;
class QSlider;
class QToolButton;

namespace QGBA {

class LibraryToolBar : public QWidget {
Q_OBJECT

public:
	explicit LibraryToolBar(QWidget* parent = nullptr);

	void setGameCounts(int all, int gba, int gbc, int gb, int sgb);
	void setViewMode(int mode);
	void setCoverSize(int size);  // restores slider position from config // 0=list 1=tree 2=grid — restores button state on startup
	int  coverSize() const;

signals:
	void filterChanged(const QGBA::LibraryFilter& filter);
	void viewModeChanged(int mode);
	void coverSizeChanged(int size);

private:
	void emitFilter();

	QButtonGroup* m_viewGroup   = nullptr;
	QComboBox*    m_platformBox = nullptr;
	QComboBox*    m_sectionBox  = nullptr;
	QLineEdit*    m_searchBar   = nullptr;
	QSlider*      m_coverSlider = nullptr;
	QWidget*      m_sliderWrap  = nullptr;

	LibraryFilter m_current;
};

} // namespace QGBA
