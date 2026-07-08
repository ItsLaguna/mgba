/* Copyright (c) 2014-2017 waddlesplash
 * Copyright (c) 2014-2024 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "library/LibraryController.h"
#include "library/moc_LibraryController.cpp"

#include "ConfigController.h"
#include "GBAApp.h"
#include "LibraryModel.h"
#include "utils.h"

#include "LibraryCoverManager.h"
#include "LibraryGridDelegate.h"

#include <QAction>
#include <QEvent>
#include <QHeaderView>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

#include <algorithm>
#include <QLineEdit>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

using namespace QGBA;

LibraryController::LibraryController(QWidget* parent, const QString& path, ConfigController* config)
	: QSplitter(Qt::Horizontal, parent)
	, m_config(config)
{
	setChildrenCollapsible(false);
	setHandleWidth(1);

	// Load persisted favorites and recently played from config
	if (m_config) {
		QString favStr = m_config->getOption("libraryFavorites");
		if (!favStr.isEmpty()) {
			for (const QString& p : favStr.split("|", Qt::SkipEmptyParts))
				m_favorites.insert(p);
		}
		QString rpStr = m_config->getOption("libraryRecentlyPlayed");
		if (!rpStr.isEmpty())
			m_recentlyPlayed = rpStr.split("|", Qt::SkipEmptyParts);
	}

	if (!path.isNull()) {
		m_library = std::shared_ptr<mLibrary>(mLibraryLoad(path.toUtf8().constData()), mLibraryDestroy);
	}
	if (!m_library) {
		m_library = std::shared_ptr<mLibrary>(mLibraryCreateEmpty(), mLibraryDestroy);
	}

	mLibraryAttachGameDB(m_library.get(), GBAApp::app()->gameDB());

	// ---- Toolbar + content in a vertical layout inside one splitter pane --
	auto* mainPane   = new QWidget(this);
	auto* mainLayout = new QVBoxLayout(mainPane);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	m_toolBar = new LibraryToolBar(mainPane);
	mainLayout->addWidget(m_toolBar);

	m_viewStack = new QStackedWidget(mainPane);
	mainLayout->addWidget(m_viewStack, 1);
	addWidget(mainPane);

	connect(m_toolBar, &LibraryToolBar::filterChanged,
	        this, &LibraryController::setFilter);
	connect(m_toolBar, &LibraryToolBar::viewModeChanged,
	        this, [this](int mode) {
		if (mode == 2) {
			setGridView(true);
		} else {
			setGridView(false);
			setViewStyle(mode == 1 ? LibraryStyle::STYLE_TREE : LibraryStyle::STYLE_LIST);
		}
		// Persist via emit so Window can save without triggering ConfigOption recursion
		emit viewModeChanged(mode);
	});
	// Debounce timer for cover size config write
	auto* coverSaveTimer = new QTimer(this);
	coverSaveTimer->setSingleShot(true);
	coverSaveTimer->setInterval(400);
	connect(coverSaveTimer, &QTimer::timeout, this, [this]() {
		if (m_config && m_gridDelegate)
			m_config->setQtOption("libraryCoverSize", m_gridDelegate->coverSize());
	});

	connect(m_toolBar, &LibraryToolBar::coverSizeChanged,
	        this, [this, coverSaveTimer](int size) {
		if (m_gridDelegate && m_gridView) {
			m_gridDelegate->setCoverSize(size);
			updateGridSize();
			m_gridView->reset();
		}
		if (m_config) coverSaveTimer->start();
	});

	// ---- Cover manager ----------------------------------------------------
	m_coverManager = new LibraryCoverManager(
		ConfigController::configDir() + "/covers", this);
	// coversChanged connection wired after m_gridView is constructed (see below)

	// ---- Model + views ----------------------------------------------------
	m_libraryModel = new LibraryModel(this);

	m_treeView = new QTreeView(m_viewStack);
	m_viewStack->addWidget(m_treeView);
	m_treeModel = new QSortFilterProxyModel(this);
	m_treeModel->setSourceModel(m_libraryModel);
	m_treeModel->setSortRole(Qt::EditRole);
	m_treeModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_treeModel->setFilterKeyColumn(LibraryModel::COL_NAME);
	m_treeView->setModel(m_treeModel);
	m_treeView->setSortingEnabled(true);
	m_treeView->setAlternatingRowColors(true);

	m_listView = new QListView(m_viewStack);
	m_viewStack->addWidget(m_listView);
	m_listModel = new QSortFilterProxyModel(this);
	m_listModel->setSourceModel(m_libraryModel);
	m_listModel->setSortRole(Qt::EditRole);
	m_listModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_listModel->setFilterKeyColumn(LibraryModel::COL_NAME);
	m_listView->setModel(m_listModel);

	// ---- Grid view (cover art) --------------------------------------------
	m_gridDelegate = new LibraryGridDelegate(m_coverManager, this);
	m_gridView = new QListView(m_viewStack);
	m_gridView->setViewMode(QListView::IconMode);
	m_gridView->setResizeMode(QListView::Adjust);
	m_gridView->setUniformItemSizes(true);
	m_gridView->setSpacing(0);
	m_gridView->setWordWrap(true);
	m_gridView->setMouseTracking(true);
	// Always-on scrollbar = stable viewport width = no flicker/reflow loop
	m_gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	m_gridView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // enables hover highlight
	m_gridView->setItemDelegate(m_gridDelegate);
	m_gridView->setGridSize(m_gridDelegate->cardSize()); // enables DuckStation-style centering
	m_gridView->setModel(m_listModel);   // list proxy ensures flat (no folders) in grid
	m_viewStack->addWidget(m_gridView);
	QObject::connect(m_gridView, &QAbstractItemView::activated, this, &LibraryController::startGame);
	connect(m_coverManager, &LibraryCoverManager::coversChanged, this, [this]() {
		m_gridView->viewport()->update();
	});
	m_gridView->viewport()->installEventFilter(this);


	// Search bar live filtering


	QObject::connect(m_treeView, &QAbstractItemView::activated, this, &LibraryController::startGame);
	QObject::connect(m_listView, &QAbstractItemView::activated, this, &LibraryController::startGame);

	// Context menu for favorites
	auto showContextMenu = [this](const QPoint& pos) {
		QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender());
		if (!view) return;
		QModelIndex idx = view->indexAt(pos);
		if (!idx.isValid()) return;
		QString fullpath = idx.data(LibraryModel::FullPathRole).toString();
		if (fullpath.isEmpty()) return;

		// Derive the filename stem (e.g. "Pokemon Crystal" from "Pokemon Crystal.gbc")
		QString filename = fullpath.section('/', -1);
		QString stem = QFileInfo(filename).completeBaseName();

		bool fav = m_favorites.contains(fullpath);
		QMenu menu(view);

		// --- Play ---
		QAction* playAction = menu.addAction(tr("Play"));
		QObject::connect(playAction, &QAction::triggered, this, [this, fullpath]() {
			selectEntry(fullpath);
			emit startGame();
		});

		menu.addSeparator();

		// --- Rename ---
		QAction* renameAction = menu.addAction(tr("Rename..."));
		QObject::connect(renameAction, &QAction::triggered, this, [this, fullpath, view]() {
			LibraryEntry e = m_libraryModel->entry(fullpath);
			if (e.isNull()) return;
			bool ok = false;
			QString current = e.displayTitle(m_showFilename);
			QString newName = QInputDialog::getText(
				view,
				tr("Rename Game"),
				tr("New name:"),
				QLineEdit::Normal,
				current,
				&ok
			);
			if (ok && !newName.isEmpty() && newName != current) {
				// Store custom name keyed by filename (not fullpath) so it
				// survives the game being moved to a different directory.
				if (m_config) {
					m_config->setQtOption(
						e.filename,
						newName,
						QStringLiteral("libraryCustomName"));
				}
				// Update the model entry title in place
				LibraryEntry updated = e;
				updated.title = newName;
				m_libraryModel->updateEntries({updated});
			}
		});

		menu.addSeparator();

		// --- Favorites ---
		QAction* favAction = menu.addAction(fav ? tr("Remove from Favorites") : tr("Add to Favorites"));
		QObject::connect(favAction, &QAction::triggered, this, [this, fullpath]() {
			toggleFavorite(fullpath);
		});

		// --- Cover art (only show if cover manager is available) ---
		if (m_coverManager) {
			menu.addSeparator();

			// Show current cover status
			QPixmap existing = m_coverManager->cover(QString(), QString(), filename);
			QString coverLabel = existing.isNull()
				? tr("Set Cover Art...")
				: tr("Change Cover Art...");
			QAction* coverAction = menu.addAction(coverLabel);
			QObject::connect(coverAction, &QAction::triggered, this, [this, stem, view]() {
				QString dest = m_coverManager->coversDir() + "/" + stem;
				QString src = QFileDialog::getOpenFileName(
					view,
					tr("Select Cover Image for %1").arg(stem),
					QString(),
					tr("Images (*.png *.jpg *.jpeg)")
				);
				if (src.isEmpty()) return;

				// Copy to covers dir, named after the filename stem
				QString ext = QFileInfo(src).suffix().toLower();
				QString destPath = dest + "." + ext;

				// Remove any existing cover for this stem (either extension)
				for (const QString& e : {QString("png"), QString("jpg"), QString("jpeg")}) {
					QFile::remove(dest + "." + e);
				}

				if (QFile::copy(src, destPath)) {
					m_coverManager->refresh();
				} else {
					// Try loading and re-saving via Qt in case of permissions/format issues
					QPixmap pix(src);
					if (!pix.isNull()) {
						pix.save(destPath);
						m_coverManager->refresh();
					}
				}
			});

			if (!existing.isNull()) {
				QAction* removeCoverAction = menu.addAction(tr("Remove Cover Art"));
				QObject::connect(removeCoverAction, &QAction::triggered, this, [this, stem]() {
					QString dest = m_coverManager->coversDir() + "/" + stem;
					for (const QString& e : {QString("png"), QString("jpg"), QString("jpeg")}) {
						QFile::remove(dest + "." + e);
					}
					m_coverManager->refresh();
				});
			}
		}

		menu.exec(view->viewport()->mapToGlobal(pos));
	};

	m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
	QObject::connect(m_treeView, &QWidget::customContextMenuRequested, this, showContextMenu);
	QObject::connect(m_listView, &QWidget::customContextMenuRequested, this, showContextMenu);
	QObject::connect(m_gridView, &QWidget::customContextMenuRequested, this, showContextMenu);
	QObject::connect(m_treeView->header(), &QHeaderView::sortIndicatorChanged, this, &LibraryController::sortChanged);

	// Save tree expand/collapse state
	QObject::connect(m_treeView, &QTreeView::expanded, this, [this](const QModelIndex&) {
		if (!m_config) return;
		// Save which paths are expanded as a list of fullpaths
		QStringList expanded;
		for (int i = 0; i < m_treeModel->rowCount(); ++i) {
			QModelIndex proxyIdx = m_treeModel->index(i, 0);
			if (m_treeView->isExpanded(proxyIdx)) {
				expanded << proxyIdx.data(Qt::DisplayRole).toString();
			}
		}
		m_config->setQtOption("libraryTreeExpanded", expanded.join("|"));
	});
	QObject::connect(m_treeView, &QTreeView::collapsed, this, [this](const QModelIndex&) {
		if (!m_config) return;
		QStringList expanded;
		for (int i = 0; i < m_treeModel->rowCount(); ++i) {
			QModelIndex proxyIdx = m_treeModel->index(i, 0);
			if (m_treeView->isExpanded(proxyIdx)) {
				expanded << proxyIdx.data(Qt::DisplayRole).toString();
			}
		}
		m_config->setQtOption("libraryTreeExpanded", expanded.join("|"));
	});

	// Save column widths whenever user drags a column divider
	QObject::connect(m_treeView->header(), &QHeaderView::sectionResized,
	        this, [this](int, int, int) {
		if (!m_config) return;
		m_userResizedColumns = true;
		m_config->setQtOption("libraryHeaderState", m_treeView->header()->saveState());
	});

	m_expandThrottle.setInterval(100);
	m_expandThrottle.setSingleShot(true);
	QObject::connect(&m_expandThrottle, &QTimer::timeout, this, [this]() { resizeTreeView(false); });
	QObject::connect(m_libraryModel, &QAbstractItemModel::modelReset, &m_expandThrottle, qOverload<>(&QTimer::start));
	QObject::connect(m_libraryModel, &QAbstractItemModel::rowsInserted, &m_expandThrottle, qOverload<>(&QTimer::start));

	// Reflow columns when splitter is dragged or viewport geometry changes (window resize fix)
	QObject::connect(this, &QSplitter::splitterMoved, this, [this]() {
		QTimer::singleShot(0, this, [this]() { resizeTreeView(false); });
	});
	QObject::connect(m_treeView->header(), &QHeaderView::geometriesChanged, this, [this]() {
		QTimer::singleShot(0, this, [this]() { resizeTreeView(false); });
	});

	connect(m_libraryModel, &QAbstractItemModel::modelReset,    this, &LibraryController::updateCountBadges);
	connect(m_libraryModel, &QAbstractItemModel::rowsInserted,  this, &LibraryController::updateCountBadges);
	connect(m_libraryModel, &QAbstractItemModel::rowsRemoved,   this, &LibraryController::updateCountBadges);

	QVariant librarySort, librarySortOrder;
	if (m_config) {
		int storedStyle = m_config->getOption("libraryStyle", int(LibraryStyle::STYLE_LIST)).toInt();
		if (storedStyle == 2) {
			// Grid view - will be applied after construction via deferred updateOption
		} else {
			updateViewStyle(static_cast<LibraryStyle>(storedStyle));
		}
		// Restore toolbar button state to match stored view
		if (m_toolBar) m_toolBar->setViewMode(storedStyle);
		librarySort = m_config->getQtOption("librarySort");
		librarySortOrder = m_config->getQtOption("librarySortOrder");
	} else {
		updateViewStyle(LibraryStyle::STYLE_LIST);
	}

	if (librarySort.isNull() || !librarySort.canConvert<int>()) {
		librarySort = 0;
	}
	if (librarySortOrder.isNull() || !librarySortOrder.canConvert<Qt::SortOrder>()) {
		librarySortOrder = Qt::AscendingOrder;
	}
	m_treeModel->sort(librarySort.toInt(), librarySortOrder.value<Qt::SortOrder>());
	m_listModel->sort(0, Qt::AscendingOrder);

	// Restore saved cover size
	if (m_config && m_toolBar && m_gridDelegate) {
		QVariant coverSize = m_config->getQtOption("libraryCoverSize");
		if (!coverSize.isNull() && coverSize.canConvert<int>()) {
			int size = coverSize.toInt();
			m_gridDelegate->setCoverSize(size);
			m_toolBar->setCoverSize(size);
		}
	}

	// Restore saved header column widths (if any)
	if (m_config) {
		QVariant headerState = m_config->getQtOption("libraryHeaderState");
		if (!headerState.isNull() && headerState.canConvert<QByteArray>()) {
			m_treeView->header()->restoreState(headerState.toByteArray());
			// Mark that user has set column widths — skip auto-resize
			m_userResizedColumns = true;
		}
	}

	refresh();
}

LibraryController::~LibraryController() {
}

void LibraryController::setViewStyle(LibraryStyle newStyle) {
	if (m_currentStyle == newStyle) {
		return;
	}
	updateViewStyle(newStyle);
}

void LibraryController::updateViewStyle(LibraryStyle newStyle) {
	// Guard: views may not be constructed yet during config option restore
	if (!m_treeView || !m_listView || !m_viewStack) {
		m_currentStyle = newStyle;
		return;
	}
	// Sync toolbar button (may be called from Window's ConfigOption handler)
	if (m_toolBar) {
		int toolbarMode = (newStyle == LibraryStyle::STYLE_TREE) ? 1 : 0;
		m_toolBar->setViewMode(toolbarMode);
	}

	QString selected;
	if (m_currentView) {
		QModelIndex selectedIndex = m_currentView->selectionModel()->currentIndex();
		if (selectedIndex.isValid()) {
			selected = selectedIndex.data(LibraryModel::FullPathRole).toString();
		}
	}

	m_currentStyle = newStyle;
	m_libraryModel->setTreeMode(newStyle == LibraryStyle::STYLE_TREE);

	QAbstractItemView* newView = m_listView;
	if (newStyle == LibraryStyle::STYLE_LIST || newStyle == LibraryStyle::STYLE_TREE) {
		newView = m_treeView;
	}

	m_viewStack->setCurrentWidget(newView);
	m_currentView = newView;
	selectEntry(selected);

	// Restore saved expand state whenever we switch to tree view
	if (newStyle == LibraryStyle::STYLE_TREE) {
		restoreTreeExpandState();
	}
}

void LibraryController::sortChanged(int column, Qt::SortOrder order) {
	if (m_config) {
		m_config->setQtOption("librarySort", column);
		m_config->setQtOption("librarySortOrder", order);
	}
}

void LibraryController::selectEntry(const QString& fullpath) {
	if (!m_currentView) {
		return;
	}
	QModelIndex index = m_libraryModel->index(fullpath);
	QAbstractProxyModel* proxy = qobject_cast<QAbstractProxyModel*>(m_currentView->model());
	if (proxy) {
		index = proxy->mapFromSource(index);
	}
	if (index.isValid()) {
		m_currentView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
	}
}

LibraryEntry LibraryController::selectedEntry() {
	if (!m_currentView) {
		return {};
	}
	QModelIndex index = m_currentView->selectionModel()->currentIndex();
	if (!index.isValid()) {
		return {};
	}
	QString fullpath = index.data(LibraryModel::FullPathRole).toString();
	return m_libraryModel->entry(fullpath);
}

VFile* LibraryController::selectedVFile() {
	LibraryEntry entry = selectedEntry();
	if (!entry.isNull()) {
		mLibraryEntry libentry = {0};
		QByteArray baseUtf8(entry.base.toUtf8());
		QByteArray filenameUtf8(entry.filename.toUtf8());
		libentry.base = baseUtf8.constData();
		libentry.filename = filenameUtf8.constData();
		libentry.platform = mPLATFORM_NONE;
		libentry.platformModels = M_LIBRARY_MODEL_UNKNOWN;
		return mLibraryOpenVFile(m_library.get(), &libentry);
	} else {
		return nullptr;
	}
}

QPair<QString, QString> LibraryController::selectedPath() {
	LibraryEntry entry = selectedEntry();
	if (!entry.isNull()) {
		return qMakePair(QString(entry.base), QString(entry.filename));
	} else {
		return qMakePair(QString(), QString());
	}
}

void LibraryController::addDirectory(const QString& dir, bool recursive) {
	std::shared_ptr<mLibrary> library = m_library;
	m_libraryJob = GBAApp::app()->submitWorkerJob(std::bind(&LibraryController::loadDirectory, this, dir, recursive), this, [this, library]() {
		// Clear any stale entries that now exist again after re-adding the folder
		if (m_config) {
			QList<QVariant> stale = m_config->getList("libraryStaleEntries");
			stale.erase(std::remove_if(stale.begin(), stale.end(),
				[](const QVariant& v) { return QFile::exists(v.toString()); }), stale.end());
			m_config->setList("libraryStaleEntries", stale);
		}
		refresh();
	});
}

void LibraryController::clear() {
	if (m_libraryJob > 0) {
		return;
	}
	mLibraryClear(m_library.get());
	refresh();
}

void LibraryController::refresh() {
	if (m_libraryJob > 0) {
		return;
	}

	setDisabled(true);

	QSet<QString> removedEntries(qListToSet(m_knownGames.keys()));
	QList<LibraryEntry> updatedEntries;
	QList<LibraryEntry> newEntries;

	// Load persisted stale paths — entries whose files no longer exist.
	// We filter these out before adding to the model so they never appear.
	QSet<QString> knownStale;
	if (m_config) {
		for (const QVariant& v : m_config->getList("libraryStaleEntries"))
			knownStale.insert(v.toString());
	}

	mLibraryListing listing;
	mLibraryListingInit(&listing, 0);
	mLibraryGetEntries(m_library.get(), &listing, 0, 0, nullptr);
	for (size_t i = 0; i < mLibraryListingSize(&listing); i++) {
		const mLibraryEntry* entry = mLibraryListingGetConstPointer(&listing, i);
		uint64_t checkHash = LibraryEntry::checkHash(entry);
		QString fullpath = QStringLiteral("%1/%2").arg(entry->base, entry->filename);

		// Skip entries whose files no longer exist on disk
		if (!QFile::exists(fullpath)) {
			removedEntries.remove(fullpath);
			m_knownGames.remove(fullpath);
			continue;
		}

		if (!m_knownGames.contains(fullpath)) {
			newEntries.append(entry);
		} else if (checkHash != m_knownGames[fullpath]) {
			updatedEntries.append(entry);
		}
		removedEntries.remove(fullpath);
		m_knownGames[fullpath] = checkHash;
	}

	for (const QString& path : removedEntries) {
		m_knownGames.remove(path);
	}

	m_libraryModel->removeEntries(removedEntries.values());
	m_libraryModel->updateEntries(updatedEntries);
	m_libraryModel->addEntries(newEntries);

	// Detect newly stale entries — files in newEntries that don't exist on disk.
	// (Previously stale entries were already filtered out above.)
	{
		QList<QString> newlyStale;
		for (const LibraryEntry& e : newEntries) {
			if (!QFile::exists(e.fullpath))
				newlyStale << e.fullpath;
		}
		// Also check updatedEntries
		for (const LibraryEntry& e : updatedEntries) {
			if (!QFile::exists(e.fullpath))
				newlyStale << e.fullpath;
		}

		if (!newlyStale.isEmpty()) {
			// Add to model removal and known stale set
			m_libraryModel->removeEntries(newlyStale);
			for (const QString& fp : newlyStale) {
				m_knownGames.remove(fp);
				knownStale.insert(fp);
			}
			// Persist using QSettings list — avoids ini corruption from path chars
			if (m_config) {
				QList<QVariant> staleList;
				for (const QString& fp : knownStale)
					staleList << fp;
				m_config->setList("libraryStaleEntries", staleList);
			}
			// Notify user once
			QTimer::singleShot(0, this, [this, count = newlyStale.size()]() {
				QMessageBox::warning(window(),
					tr("Library"),
					tr("%n game(s) could not be found and have been removed from the library. "
					   "If you moved your ROM folder, use \"Add Folder to Library\" to re-add it.",
					   "", count));
			});
		}
	}

	// Restore any custom names saved from previous rename operations.
	// Keyed by filename so names survive the ROM being moved to a different path.
	if (m_config) {
		QList<LibraryEntry> renamedEntries;
		int rows = m_libraryModel->rowCount();
		for (int i = 0; i < rows; ++i) {
			QString fp = m_libraryModel->index(i, 0).data(LibraryModel::FullPathRole).toString();
			if (fp.isEmpty()) continue;
			LibraryEntry e = m_libraryModel->entry(fp);
			if (e.isNull()) continue;
			QVariant custom = m_config->getQtOption(e.filename, QStringLiteral("libraryCustomName"));
			if (!custom.isNull() && !custom.toString().isEmpty()) {
				e.title = custom.toString();
				renamedEntries << e;
			}
		}
		if (!renamedEntries.isEmpty())
			m_libraryModel->updateEntries(renamedEntries);
	}

	// One-time migration: re-key any old fullpath-based custom names to filename-based.
	// Reads all keys from [libraryCustomName], and if a key looks like a fullpath
	// (contains '/'), re-saves it under just the filename portion.
	if (m_config) {
		m_config->getQtOption("__migrated__", "libraryCustomName"); // touch group
		// Use QSettings directly via getQtOption to enumerate keys
		// We do this by reading all known game filenames and checking for fullpath keys
		int rows = m_libraryModel->rowCount();
		for (int i = 0; i < rows; ++i) {
			QString fp = m_libraryModel->index(i, 0).data(LibraryModel::FullPathRole).toString();
			if (fp.isEmpty()) continue;
			// Check if there's a fullpath-keyed entry
			QVariant oldVal = m_config->getQtOption(fp, QStringLiteral("libraryCustomName"));
			if (!oldVal.isNull() && !oldVal.toString().isEmpty()) {
				QString filename = fp.section('/', -1);
				// Re-save under filename key and remove the fullpath key
				m_config->setQtOption(filename, oldVal, QStringLiteral("libraryCustomName"));
				// Remove old key by setting empty — QSettings doesn't have remove via ConfigController
				// so we overwrite with empty and skip it in restore
				m_config->setQtOption(fp, QString(), QStringLiteral("libraryCustomName"));
			}
		}
	}

	for (size_t i = 0; i < mLibraryListingSize(&listing); ++i) {
		mLibraryEntryFree(mLibraryListingGetPointer(&listing, i));
	}
	mLibraryListingDeinit(&listing);

	setDisabled(false);
	selectLastBootedGame();

	// Restore tree expand state after model is populated
	if (m_libraryModel->treeMode()) {
		restoreTreeExpandState();
	}

	emit doneLoading();
}

void LibraryController::selectLastBootedGame() {
	if (!m_config || m_config->getMRU().isEmpty()) {
		return;
	}
	const QString lastfile = m_config->getMRU().first();
	if (m_knownGames.contains(lastfile)) {
		selectEntry(lastfile);
	}
}

void LibraryController::loadDirectory(const QString& dir, bool recursive) {
	std::shared_ptr<mLibrary> library = m_library;
	qint64 libraryJob = m_libraryJob;
	mLibraryLoadDirectory(library.get(), dir.toUtf8().constData(), recursive);
	m_libraryJob.testAndSetOrdered(libraryJob, -1);
}

void LibraryController::setShowFilename(bool showFilename) {
	if (showFilename == m_showFilename) {
		return;
	}
	m_showFilename = showFilename;
	m_libraryModel->setShowFilename(m_showFilename);
	refresh();
}

void LibraryController::setFilter(const LibraryFilter& filter) {
	m_activeFilter = filter;

	// Build allow-set for section filters (Recently Played / Favorites)
	QSet<QString> allowSet;
	bool hasAllowSet = false;

	if (filter.section == LibraryFilter::Section::RecentlyPlayed) {
		allowSet = QSet<QString>(m_recentlyPlayed.begin(), m_recentlyPlayed.end());
		hasAllowSet = true;
	} else if (filter.section == LibraryFilter::Section::Favorites) {
		allowSet = m_favorites;
		hasAllowSet = true;
	}

	for (QSortFilterProxyModel* proxy : {m_treeModel, m_listModel}) {
		if (hasAllowSet) {
			// Filter by FullPathRole using a regex alternation of allowed paths
			proxy->setFilterRole(LibraryModel::FullPathRole);
			if (allowSet.isEmpty()) {
				// Nothing matches — use an unmatchable pattern
				proxy->setFilterRegularExpression(QRegularExpression("^$(?!.)"));
			} else {
				QStringList escaped;
				for (const QString& p : allowSet)
					escaped << QRegularExpression::escape(p);
				proxy->setFilterRegularExpression(
					QRegularExpression("^(" + escaped.join("|") + ")$"));
			}
		} else if (!filter.platform.isEmpty()) {
			proxy->setFilterRole(Qt::DisplayRole);
			proxy->setFilterKeyColumn(LibraryModel::COL_PLATFORM);
			proxy->setFilterFixedString(filter.platform);
		} else {
			proxy->setFilterRole(Qt::DisplayRole);
			proxy->setFilterKeyColumn(LibraryModel::COL_NAME);
			proxy->setFilterFixedString(filter.searchTerm);
		}
	}
}

void LibraryController::setGridView(bool grid) {
	m_gridViewActive = grid;
	if (!m_gridView || !m_viewStack) return; // not yet constructed
	if (grid) {
		m_libraryModel->setTreeMode(false);
		m_viewStack->setCurrentWidget(m_gridView);
		m_currentView = m_gridView;
		// Sync toolbar button
		if (m_toolBar) m_toolBar->setViewMode(2);
	} else {
		updateViewStyle(m_currentStyle); // updateViewStyle syncs toolbar for 0/1
	}
	// Do NOT call m_config->setOption here — causes infinite recursion via ConfigOption.
}

void LibraryController::refreshCovers() {
	if (m_coverManager) {
		m_coverManager->refresh();
	}
}

void LibraryController::recordGameLaunched(const QString& fullpath) {
	if (fullpath.isEmpty()) return;
	m_recentlyPlayed.removeAll(fullpath);
	m_recentlyPlayed.prepend(fullpath);
	while (m_recentlyPlayed.size() > 50) m_recentlyPlayed.removeLast();
	if (m_config) {
		m_config->setOption("libraryRecentlyPlayed", m_recentlyPlayed.join("|"));
	}
	// Refresh view if currently showing recently played
	if (m_activeFilter.section == LibraryFilter::Section::RecentlyPlayed) {
		setFilter(m_activeFilter);
	}
}

void LibraryController::toggleFavorite(const QString& fullpath) {
	if (fullpath.isEmpty()) return;
	if (m_favorites.contains(fullpath)) {
		m_favorites.remove(fullpath);
	} else {
		m_favorites.insert(fullpath);
	}
	if (m_config) {
		m_config->setOption("libraryFavorites", QStringList(m_favorites.values()).join("|"));
	}
	emit entryFavorited(fullpath, m_favorites.contains(fullpath));
	// Refresh view if currently showing favorites
	if (m_activeFilter.section == LibraryFilter::Section::Favorites) {
		setFilter(m_activeFilter);
	}
}

void LibraryController::restoreTreeExpandState() {
	if (!m_config || !m_treeView) return;
	QVariant expandedVar = m_config->getQtOption("libraryTreeExpanded");
	if (expandedVar.isNull()) return;
	QStringList expandedPaths = expandedVar.toString().split("|", Qt::SkipEmptyParts);
	for (int i = 0; i < m_treeModel->rowCount(); ++i) {
		QModelIndex proxyIdx = m_treeModel->index(i, 0);
		bool shouldExpand = expandedPaths.contains(proxyIdx.data(Qt::DisplayRole).toString());
		if (shouldExpand) {
			m_treeView->expand(proxyIdx);
		} else {
			m_treeView->collapse(proxyIdx);
		}
	}
}

void LibraryController::updateCountBadges() {
	if (!m_toolBar) {
		return;
	}

	int all = 0, gba = 0, gbc = 0, gb = 0, sgb = 0;
	int rows = m_libraryModel->rowCount();

	for (int i = 0; i < rows; ++i) {
		if (m_libraryModel->treeMode()) {
			QModelIndex parent = m_libraryModel->index(i, 0);
			int children = m_libraryModel->rowCount(parent);
			all += children;
			for (int j = 0; j < children; ++j) {
				QString plat = m_libraryModel->index(j, LibraryModel::COL_PLATFORM, parent).data().toString();
				if      (plat == "GBA") ++gba;
				else if (plat == "GBC") ++gbc;
				else if (plat == "GB")  ++gb;
				else if (plat == "SGB") ++sgb;
			}
		} else {
			++all;
			QString plat = m_libraryModel->index(i, LibraryModel::COL_PLATFORM).data().toString();
			if      (plat == "GBA") ++gba;
			else if (plat == "GBC") ++gbc;
			else if (plat == "GB")  ++gb;
			else if (plat == "SGB") ++sgb;
		}
	}

	if (m_toolBar) m_toolBar->setGameCounts(all, gba, gbc, gb, sgb);
}

void LibraryController::updateGridSize() {
	if (!m_gridView || !m_gridDelegate) return;
	const int viewportW = m_gridView->viewport()->width();
	const int minCardW  = m_gridDelegate->cardSize().width();
	if (minCardW <= 0 || viewportW <= 0) return;

	// Fixed number of columns based on current card size and viewport width.
	// We never change this mid-paint — no flickering.
	// Use viewportW - 1 to prevent a column from being just 1px too wide
	// and causing the scrollbar to appear/disappear
	const int safeW = viewportW - 1;
	const int cols  = qMax(1, safeW / minCardW);
	const int cellW = safeW / cols;
	const int cellH = m_gridDelegate->cardSize().height();

	m_gridView->setUpdatesEnabled(false);
	m_gridView->setGridSize(QSize(cellW, cellH));
	m_gridView->setUpdatesEnabled(true);
}

bool LibraryController::eventFilter(QObject* obj, QEvent* event) {
	// Only handle viewport resize; ignore everything else to avoid loops
	if (m_gridView && obj == m_gridView->viewport()
	        && event->type() == QEvent::Resize) {
		updateGridSize();  // synchronous, no singleShot — prevents feedback loop
	}
	return QSplitter::eventFilter(obj, event);
}

void LibraryController::showEvent(QShowEvent* event) {
	QSplitter::showEvent(event);
	QTimer::singleShot(0, this, [this]() { resizeTreeView(false); });
}

void LibraryController::resizeEvent(QResizeEvent* event) {
	QSplitter::resizeEvent(event);
	// Defer: viewport geometry isn't updated until after this event returns
	QTimer::singleShot(0, this, [this]() { resizeTreeView(false); });
}

void LibraryController::resizeTreeView(bool expand) {
	if (expand) {
		m_treeView->expandAll();
	}

	// Don't auto-resize columns if the user has manually set them
	if (m_userResizedColumns) return;

	int viewportWidth = m_treeView->viewport()->width();
	int totalWidth = m_treeView->header()->sectionSizeHint(LibraryModel::MAX_COLUMN);
	for (int column = 0; column < LibraryModel::MAX_COLUMN; column++) {
		totalWidth += m_treeView->columnWidth(column);
	}

	if (totalWidth < viewportWidth) {
		totalWidth = 0;
		for (int column = 0; column <= LibraryModel::MAX_COLUMN; column++) {
			m_treeView->resizeColumnToContents(column);
			totalWidth += m_treeView->columnWidth(column);
		}
	}

	if (totalWidth > viewportWidth) {
		int locationWidth = m_treeView->columnWidth(LibraryModel::COL_LOCATION);
		if (locationWidth > 100) {
			int newLocationWidth = m_treeView->viewport()->width() - (totalWidth - locationWidth);
			if (newLocationWidth < 100) {
				newLocationWidth = 100;
			}
			m_treeView->setColumnWidth(LibraryModel::COL_LOCATION, newLocationWidth);
		}
	}
}
