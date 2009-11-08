/*
 * Bittorrent Client using Qt4 and libtorrent.
 * Copyright (C) 2006  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */

#include "TransferListWidget.h"
#include "TransferListDelegate.h"
#include "bittorrent.h"
#include "torrentPersistentData.h"
#include "previewSelect.h"
#include "allocationDlg.h"
#include "options_imp.h"
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QDesktopServices>
#include <QTimer>
#include <QSettings>
#include <QClipboard>
#include <QColor>
#include <QUrl>
#include <QMenu>
#include <vector>

TransferListWidget::TransferListWidget(QWidget *parent, bittorrent *_BTSession): QTreeView(parent) {
  QSettings settings("qBittorrent", "qBittorrent");
  BTSession = _BTSession;

  // Create and apply delegate
  listDelegate = new TransferListDelegate(this);
  setItemDelegate(listDelegate);

  // Create transfer list model
  listModel = new QStandardItemModel(0,10);
  listModel->setHeaderData(NAME, Qt::Horizontal, tr("Name", "i.e: file name"));
  listModel->setHeaderData(SIZE, Qt::Horizontal, tr("Size", "i.e: file size"));
  listModel->setHeaderData(PROGRESS, Qt::Horizontal, tr("Progress", "i.e: % downloaded"));
  listModel->setHeaderData(DLSPEED, Qt::Horizontal, tr("DL Speed", "i.e: Download speed"));
  listModel->setHeaderData(UPSPEED, Qt::Horizontal, tr("UP Speed", "i.e: Upload speed"));
  listModel->setHeaderData(SEEDSLEECH, Qt::Horizontal, tr("Seeds/Leechers", "i.e: full/partial sources"));
  listModel->setHeaderData(RATIO, Qt::Horizontal, tr("Ratio"));
  listModel->setHeaderData(ETA, Qt::Horizontal, tr("ETA", "i.e: Estimated Time of Arrival / Time left"));
  listModel->setHeaderData(PRIORITY, Qt::Horizontal, "#");

  // Set Sort/Filter proxy
  proxyModel = new QSortFilterProxyModel();
  proxyModel->setDynamicSortFilter(true);
  proxyModel->setSourceModel(listModel);
  setModel(proxyModel);

  // Visual settings
  setRootIsDecorated(false);
  setAllColumnsShowFocus(true);
  setSortingEnabled(true);
  hideColumn(PRIORITY);
  hideColumn(HASH);
  loadHiddenColumns();
  // Load last columns width for transfer list
  if(!loadColWidthList()) {
    header()->resizeSection(0, 200);
  }
  loadLastSortedColumn();
  setContextMenuPolicy(Qt::CustomContextMenu);

  // Listen for BTSession events
  connect(BTSession, SIGNAL(addedTorrent(QTorrentHandle&)), this, SLOT(addTorrent(QTorrentHandle&)));
  connect(BTSession, SIGNAL(finishedTorrent(QTorrentHandle&)), this, SLOT(setFinished(QTorrentHandle&)));
  connect(BTSession, SIGNAL(metadataReceived(QTorrentHandle&)), this, SLOT(updateMetadata(QTorrentHandle&)));

  // Listen for list events
  connect(this, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(torrentDoubleClicked(QModelIndex)));
  connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayListMenu(const QPoint&)));
  connect(header(), SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(displayDLHoSMenu(const QPoint&)));

  // Refresh timer
  refreshTimer = new QTimer();
  refreshTimer->start(settings.value("Preferences/General/RefreshInterval", 1500).toInt());
  connect(refreshTimer, SIGNAL(timeout()), this, SLOT(refreshList()));
}

TransferListWidget::~TransferListWidget() {
  // Save settings
  saveLastSortedColumn();
  saveColWidthList();
  saveHiddenColumns();
  // Clean up
  delete refreshTimer;
  delete proxyModel;
  delete listModel;
  delete listDelegate;
}

void TransferListWidget::addTorrent(QTorrentHandle& h) {
  if(!h.is_valid()) return;
  int row = listModel->rowCount();
  try {
    // Adding torrent to transfer list
    listModel->insertRow(row);
    listModel->setData(listModel->index(row, NAME), QVariant(h.name()));
    listModel->setData(listModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
    if(BTSession->isQueueingEnabled() && !h.is_seed())
      listModel->setData(listModel->index(row, PRIORITY), QVariant((int)BTSession->getDlTorrentPriority(h.hash())));
    listModel->setData(listModel->index(row, HASH), QVariant(h.hash()));
    // Pause torrent if it is
    if(h.is_paused()) {
      listModel->setData(listModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/paused.png"))), Qt::DecorationRole);
      //setRowColor(row, QString::fromUtf8("red"));
    }else{
      listModel->setData(listModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/stalled.png"))), Qt::DecorationRole);
      //setRowColor(row, QString::fromUtf8("grey"));
    }
  } catch(invalid_handle e) {
    // Remove added torrent
    listModel->removeRow(row);
  }
}

/*void TransferListWidget::setRowColor(int row, QColor color) {
  unsigned int nbColumns = listModel->columnCount()-1;
  for(unsigned int i=0; i<nbColumns; ++i) {
    listModel->setData(listModel->index(row, i), QVariant(color), Qt::ForegroundRole);
  }
}*/

void TransferListWidget::deleteTorrent(int row) {
  listModel->removeRow(row);
}

void TransferListWidget::pauseTorrent(QString hash) {
  pauseTorrent(getRowFromHash(hash));
}

void TransferListWidget::pauseTorrent(int row) {
  listModel->setData(listModel->index(row, DLSPEED), QVariant((double)0.0));
  listModel->setData(listModel->index(row, UPSPEED), QVariant((double)0.0));
  listModel->setData(listModel->index(row, ETA), QVariant((qlonglong)-1));
  listModel->setData(listModel->index(row, NAME), QIcon(QString::fromUtf8(":/Icons/skin/paused.png")), Qt::DecorationRole);
  listModel->setData(listModel->index(row, SEEDSLEECH), QVariant(QString::fromUtf8("0/0")));
  //setRowColor(row, QString::fromUtf8("red"));
}

void TransferListWidget::resumeTorrent(int row) {
  QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(row));
  if(!h.is_valid()) return;
  if(h.is_seed())
    listModel->setData(listModel->index(row, NAME), QVariant(QIcon(":/Icons/skin/seeding.png")), Qt::DecorationRole);
  updateTorrent(row);
}

void TransferListWidget::updateMetadata(QTorrentHandle &h) {
  QString hash = h.hash();
  int row = getRowFromHash(hash);
  if(row != -1) {
    qDebug("Updating torrent metadata in download list");
    listModel->setData(listModel->index(row, NAME), QVariant(h.name()));
    listModel->setData(listModel->index(row, SIZE), QVariant((qlonglong)h.actual_size()));
  }
}

void TransferListWidget::updateTorrent(int row) {
  QString hash = getHashFromRow(row);
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  if(!h.is_valid()) {
    // Delete torrent
    deleteTorrent(row);
    return;
  }
  try {
    if(!h.is_seed()) {
      // Queueing code
      if(BTSession->isQueueingEnabled()) {
        listModel->setData(listModel->index(row, PRIORITY), QVariant((int)BTSession->getDlTorrentPriority(hash)));
        if(h.is_queued()) {
          if(h.state() == torrent_status::checking_files || h.state() == torrent_status::queued_for_checking) {
            listModel->setData(listModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/oxygen/run-build.png"))), Qt::DecorationRole);
            listModel->setData(listModel->index(row, PROGRESS), QVariant((double)h.progress()));
          }else {
            listModel->setData(listModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/oxygen/mail-queue.png"))), Qt::DecorationRole);
            listModel->setData(listModel->index(row, ETA), QVariant((qlonglong)-1));
          }
          // Reset speeds and seeds/leech
          listModel->setData(listModel->index(row, DLSPEED), QVariant((double)0.));
          listModel->setData(listModel->index(row, UPSPEED), QVariant((double)0.));
          listModel->setData(listModel->index(row, SEEDSLEECH), QVariant("0/0"));
          //setRowColor(row, QString::fromUtf8("grey"));
          return;
        }
      }
      if(h.is_paused()) return;
      // Update
      listModel->setData(listModel->index(row, PROGRESS), QVariant((double)h.progress()));
      listModel->setData(listModel->index(row, DLSPEED), QVariant((double)h.download_payload_rate()));

      // Parse download state
      switch(h.state()) {
      case torrent_status::checking_files:
      case torrent_status::queued_for_checking:
      case torrent_status::checking_resume_data:
        listModel->setData(listModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/oxygen/run-build.png"))), Qt::DecorationRole);
        //setRowColor(row, QString::fromUtf8("grey"));
        break;
      case torrent_status::downloading:
      case torrent_status::downloading_metadata:
        if(h.download_payload_rate() > 0) {
          listModel->setData(listModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/downloading.png"))), Qt::DecorationRole);
          listModel->setData(listModel->index(row, ETA), QVariant((qlonglong)BTSession->getETA(hash)));
          //setRowColor(row, QString::fromUtf8("green"));
        }else{
          listModel->setData(listModel->index(row, NAME), QVariant(QIcon(QString::fromUtf8(":/Icons/skin/stalled.png"))), Qt::DecorationRole);
          listModel->setData(listModel->index(row, ETA), QVariant((qlonglong)-1));
          //setRowColor(row, QApplication::palette().color(QPalette::WindowText));
        }
        listModel->setData(listModel->index(row, UPSPEED), QVariant((double)h.upload_payload_rate()));
        break;
      default:
        listModel->setData(listModel->index(row, ETA), QVariant((qlonglong)-1));
      }
    }

    // Common to both downloads and uploads
    QString tmp = misc::toQString(h.num_seeds(), true);
    if(h.num_complete() >= 0)
      tmp.append(QString("(")+misc::toQString(h.num_complete())+QString(")"));
    tmp.append(QString("/")+misc::toQString(h.num_peers() - h.num_seeds(), true));
    if(h.num_incomplete() >= 0)
      tmp.append(QString("(")+misc::toQString(h.num_incomplete())+QString(")"));
    listModel->setData(listModel->index(row, SEEDSLEECH), QVariant(tmp));
    listModel->setData(listModel->index(row, RATIO), QVariant(misc::toQString(BTSession->getRealRatio(hash))));
    listModel->setData(listModel->index(row, UPSPEED), QVariant((double)h.upload_payload_rate()));
    // FIXME: Add all_time_upload column
  }catch(invalid_handle e) {
    deleteTorrent(row);
    qDebug("Caught Invalid handle exception, lucky us.");
  }
}

void TransferListWidget::setFinished(QTorrentHandle &h) {
  int row = -1;
  try {
    row = getRowFromHash(h.hash());
    if(row >= 0) {
      if(h.is_paused()) {
        listModel->setData(listModel->index(row, NAME), QIcon(":/Icons/skin/paused.png"), Qt::DecorationRole);
        //setRowColor(row, "red");
      }else{
        listModel->setData(listModel->index(row, NAME), QVariant(QIcon(":/Icons/skin/seeding.png")), Qt::DecorationRole);
        //setRowColor(row, "orange");
      }
      listModel->setData(listModel->index(row, ETA), QVariant((qlonglong)-1));
      listModel->setData(listModel->index(row, DLSPEED), QVariant((double)0.));
      listModel->setData(listModel->index(row, PROGRESS), QVariant((double)1.));
      listModel->setData(listModel->index(row, PRIORITY), QVariant((int)-1));
    }
  } catch(invalid_handle e) {
    if(row >= 0) deleteTorrent(row);
  }
}

void TransferListWidget::setRefreshInterval(int t) {
  refreshTimer->start(t);
}

void TransferListWidget::refreshList() {
  for(int i=0; i<listModel->rowCount(); ++i) {
    updateTorrent(i);
  }
}

int TransferListWidget::getRowFromHash(QString hash) const{
  QList<QStandardItem *> items = listModel->findItems(hash, Qt::MatchExactly, HASH);
  if(items.empty()) return -1;
  Q_ASSERT(items.size() == 1);
  return items.first()->row();
}

QString TransferListWidget::getHashFromRow(int row) const {
  return listModel->data(listModel->index(row, HASH)).toString();
}

void TransferListWidget::torrentDoubleClicked(QModelIndex index) {
  int row = proxyModel->mapToSource(index).row();
  QString hash = getHashFromRow(row);
  QTorrentHandle h = BTSession->getTorrentHandle(hash);
  if(!h.is_valid()) return;
  int action;
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  if(h.is_seed()) {
    action =  settings.value(QString::fromUtf8("Preferences/Downloads/DblClOnTorFN"), 0).toInt();
  } else {
    action = settings.value(QString::fromUtf8("Preferences/Downloads/DblClOnTorDl"), 0).toInt();
  }

  switch(action) {
  case TOGGLE_PAUSE:
    if(h.is_paused()) {
      h.resume();
      resumeTorrent(row);
    } else {
      h.pause();
      pauseTorrent(row);
    }
    break;
  case OPEN_DEST:
    QDesktopServices::openUrl(QUrl(h.save_path()));
    break;
  }
}

void TransferListWidget::startSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  foreach(const QModelIndex &index, selectedIndexes) {
    // Get the file hash
    QString hash = getHashFromRow(index.row());
    QTorrentHandle h = BTSession->getTorrentHandle(hash);
    if(h.is_valid() && h.is_paused()) {
      h.resume();
      resumeTorrent(index.row());
    }
  }
}

void TransferListWidget::startAllTorrents() {
  for(int i=0; i<listModel->rowCount(); ++i) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(i));
    if(h.is_valid() && h.is_paused()) {
      h.resume();
      resumeTorrent(i);
    }
  }
}

void TransferListWidget::pauseSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  foreach(const QModelIndex &index, selectedIndexes) {
    // Get the file hash
    QString hash = getHashFromRow(index.row());
    QTorrentHandle h = BTSession->getTorrentHandle(hash);
    if(h.is_valid() && !h.is_paused()) {
      h.pause();
      pauseTorrent(index.row());
    }
  }
}

void TransferListWidget::pauseAllTorrents() {
  for(int i=0; i<listModel->rowCount(); ++i) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(i));
    if(h.is_valid() && !h.is_paused()) {
      h.pause();
      pauseTorrent(i);
    }
  }
}

void TransferListWidget::deleteSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  if(!selectedIndexes.empty()) {
    int ret = QMessageBox::question(
        this,
        tr("Deletion confirmation"),
        tr("Are you sure you want to delete the selected torrents from transfer list?"),
        tr("&Yes"), tr("&No"),
        QString(), 0, 1);
    if(ret) return;
    foreach(const QModelIndex &index, selectedIndexes) {
      // Get the file hash
      QString hash = getHashFromRow(index.row());
      deleteTorrent(index.row());
      BTSession->deleteTorrent(hash, false);
    }
  }
}

void TransferListWidget::deletePermSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  if(!selectedIndexes.empty()) {
    int ret = QMessageBox::question(
        this,
        tr("Deletion confirmation"),
        tr("Are you sure you want to delete the selected torrents from transfe list and hard disk?"),
        tr("&Yes"), tr("&No"),
        QString(), 0, 1);
    if(ret) return;
    foreach(const QModelIndex &index, selectedIndexes) {
      // Get the file hash
      QString hash = getHashFromRow(index.row());
      deleteTorrent(index.row());
      BTSession->deleteTorrent(hash, true);
    }
  }
}

// FIXME: Should work only if the tab is displayed
void TransferListWidget::increasePrioSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  foreach(const QModelIndex &index, selectedIndexes) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(index.row()));
    if(h.is_valid() && !h.is_seed()) {
      BTSession->increaseDlTorrentPriority(h.hash());
    }
  }
  refreshList();
}

// FIXME: Should work only if the tab is displayed
void TransferListWidget::decreasePrioSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  foreach(const QModelIndex &index, selectedIndexes) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(index.row()));
    if(h.is_valid() && !h.is_seed()) {
      BTSession->decreaseDlTorrentPriority(h.hash());
    }
  }
  refreshList();
}

void TransferListWidget::buySelectedTorrents() const {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  foreach(const QModelIndex &index, selectedIndexes) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(index.row()));
    if(h.is_valid())
      QDesktopServices::openUrl("http://match.sharemonkey.com/?info_hash="+h.hash()+"&n="+h.name()+"&cid=33");
  }
}

void TransferListWidget::copySelectedMagnetURIs() const {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  QStringList magnet_uris;
  foreach(const QModelIndex &index, selectedIndexes) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(index.row()));
    if(h.is_valid())
      magnet_uris << misc::toQString(make_magnet_uri(h.get_torrent_info()));
  }
  qApp->clipboard()->setText(magnet_uris.join("\n"));
}

void TransferListWidget::hidePriorityColumn(bool hide) {
  setColumnHidden(PRIORITY, hide);
}

void TransferListWidget::openSelectedTorrentsFolder() const {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  QStringList pathsList;
  foreach(const QModelIndex &index, selectedIndexes) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(index.row()));
    if(h.is_valid()) {
      QString savePath = h.save_path();
      if(!pathsList.contains(savePath)) {
        pathsList.append(savePath);
        QDesktopServices::openUrl(QUrl(QString("file://")+savePath));
      }
    }
  }
}

void TransferListWidget::previewSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  QStringList pathsList;
  foreach(const QModelIndex &index, selectedIndexes) {
    QTorrentHandle h = BTSession->getTorrentHandle(getHashFromRow(index.row()));
    if(h.is_valid()) {
      new previewSelect(this, h);
    }
  }
}

void TransferListWidget::setDlLimitSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  QStringList hashes;
  foreach(const QModelIndex &index, selectedIndexes) {
    // Get the file hash
    QString hash = getHashFromRow(index.row());
    QTorrentHandle h = BTSession->getTorrentHandle(hash);
    if(h.is_valid() && !h.is_seed())
      hashes << hash;
  }
  Q_ASSERT(hashes.size() > 0);
  new BandwidthAllocationDialog(this, false, BTSession, hashes);
}

void TransferListWidget::setUpLimitSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  QStringList hashes;
  foreach(const QModelIndex &index, selectedIndexes) {
    // Get the file hash
    hashes << getHashFromRow(index.row());
  }
  Q_ASSERT(hashes.size() > 0);
  new BandwidthAllocationDialog(this, true, BTSession, hashes);
}

void TransferListWidget::recheckSelectedTorrents() {
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  foreach(const QModelIndex &index, selectedIndexes){
    QString hash = getHashFromRow(index.row());
    QTorrentHandle h = BTSession->getTorrentHandle(hash);
    if(h.is_valid() && h.has_metadata())
      h.force_recheck();
  }
}

// save the hidden columns in settings
void TransferListWidget::saveHiddenColumns() {
  QSettings settings("qBittorrent", "qBittorrent");
  QStringList ishidden_list;
  short nbColumns = listModel->columnCount()-1;

  for(short i=0; i<nbColumns; ++i){
    if(isColumnHidden(i)) {
      ishidden_list << "0";
    } else {
      ishidden_list << "1";
    }
  }
  settings.setValue("TransferListColsHoS", ishidden_list.join(" "));
}

// load the previous settings, and hide the columns
bool TransferListWidget::loadHiddenColumns() {
  QSettings settings("qBittorrent", "qBittorrent");
  QString line = settings.value("TransferListColsHoS", QString()).toString();
  if(line.isEmpty()) return false;
  bool loaded = false;
  QStringList ishidden_list;
  ishidden_list = line.split(' ');
  unsigned int nbCol = ishidden_list.size();
  if(nbCol == (unsigned int)listModel->columnCount()-1) {
    for(unsigned int i=0; i<nbCol; ++i){
      if(ishidden_list.at(i) == "0") {
        setColumnHidden(i, true);
      }
    }
    loaded = true;
  }
  return loaded;
}

// hide/show columns menu
void TransferListWidget::displayDLHoSMenu(const QPoint&){
  QMenu hideshowColumn(this);
  hideshowColumn.setTitle(tr("Column visibility"));
  int lastCol;
  if(BTSession->isQueueingEnabled()) {
    lastCol = PRIORITY;
  } else {
    lastCol = ETA;
  }
  QList<QAction*> actions;
  for(int i=0; i <= lastCol; ++i) {
    QIcon icon;
    if(isColumnHidden(i))
      icon = QIcon(QString::fromUtf8(":/Icons/oxygen/button_cancel.png"));
    else
      icon = QIcon(QString::fromUtf8(":/Icons/oxygen/button_ok.png"));
    actions.append(hideshowColumn.addAction(icon, listModel->headerData(i, Qt::Horizontal).toString()));
  }
  // Call menu
  QAction *act = hideshowColumn.exec(QCursor::pos());
  int col = actions.indexOf(act);
  setColumnHidden(col, !isColumnHidden(col));
}

void TransferListWidget::displayListMenu(const QPoint&) {
  // Create actions
  QAction actionStart(QIcon(QString::fromUtf8(":/Icons/skin/play.png")), tr("Start"), 0);
  connect(&actionStart, SIGNAL(triggered()), this, SLOT(startSelectedTorrents()));
  QAction actionPause(QIcon(QString::fromUtf8(":/Icons/skin/pause.png")), tr("Pause"), 0);
  connect(&actionPause, SIGNAL(triggered()), this, SLOT(pauseSelectedTorrents()));
  QAction actionDelete(QIcon(QString::fromUtf8(":/Icons/skin/delete.png")), tr("Delete"), 0);
  connect(&actionDelete, SIGNAL(triggered()), this, SLOT(deleteSelectedTorrents()));
  QAction actionPreview_file(QIcon(QString::fromUtf8(":/Icons/skin/preview.png")), tr("Preview file"), 0);
  connect(&actionPreview_file, SIGNAL(triggered()), this, SLOT(previewSelectedTorrents()));
  QAction actionSet_upload_limit(QIcon(QString::fromUtf8(":/Icons/skin/seeding.png")), tr("Set upload limit"), 0);
  connect(&actionSet_upload_limit, SIGNAL(triggered()), this, SLOT(setUpLimitSelectedTorrents()));
  QAction actionSet_download_limit(QIcon(QString::fromUtf8(":/Icons/skin/downloading.png")), tr("Set download limit"), 0);
  connect(&actionSet_download_limit, SIGNAL(triggered()), this, SLOT(setDlLimitSelectedTorrents()));
  QAction actionDelete_Permanently(QIcon(QString::fromUtf8(":/Icons/skin/delete_perm.png")), tr("Delete Permanently"), 0);
  connect(&actionDelete_Permanently, SIGNAL(triggered()), this, SLOT(deletePermSelectedTorrents()));
  QAction actionOpen_destination_folder(QIcon(QString::fromUtf8(":/Icons/oxygen/folder.png")), tr("Open destination folder"), 0);
  connect(&actionOpen_destination_folder, SIGNAL(triggered()), this, SLOT(openSelectedTorrentsFolder()));
  QAction actionBuy_it(QIcon(QString::fromUtf8(":/Icons/oxygen/wallet.png")), tr("Buy it"), 0);
  connect(&actionBuy_it, SIGNAL(triggered()), this, SLOT(buySelectedTorrents()));
  QAction actionIncreasePriority(QIcon(QString::fromUtf8(":/Icons/skin/increase.png")), tr("Increase priority"), 0);
  connect(&actionIncreasePriority, SIGNAL(triggered()), this, SLOT(increasePrioSelectedTorrents()));
  QAction actionDecreasePriority(QIcon(QString::fromUtf8(":/Icons/skin/decrease.png")), tr("Decrease priority"), 0);
  connect(&actionDecreasePriority, SIGNAL(triggered()), this, SLOT(decreasePrioSelectedTorrents()));
  QAction actionForce_recheck(QIcon(QString::fromUtf8(":/Icons/oxygen/gear.png")), tr("Force recheck"), 0);
  connect(&actionForce_recheck, SIGNAL(triggered()), this, SLOT(recheckSelectedTorrents()));
  QAction actionCopy_magnet_link(QIcon(QString::fromUtf8(":/Icons/magnet.png")), tr("Copy magnet link"), 0);
  connect(&actionCopy_magnet_link, SIGNAL(triggered()), this, SLOT(copySelectedMagnetURIs()));
  // End of actions
  QMenu listMenu(this);
  // Enable/disable pause/start action given the DL state
  QModelIndexList selectedIndexes = selectionModel()->selectedRows();
  bool has_pause = false, has_start = false, has_preview = false;
  bool one_has_metadata = false, one_not_seed = false;
  QTorrentHandle h;
  qDebug("Displaying menu");
  foreach(const QModelIndex &index, selectedIndexes) {
    // Get the file name
    QString hash = getHashFromRow(index.row());
    // Get handle and pause the torrent
    h = BTSession->getTorrentHandle(hash);
    if(!h.is_valid()) continue;
    if(h.has_metadata())
      one_has_metadata = true;
    if(!h.is_seed())
      one_not_seed = true;
    if(h.is_paused()) {
      if(!has_start) {
        listMenu.addAction(&actionStart);
        has_start = true;
      }
    }else{
      if(!has_pause) {
        listMenu.addAction(&actionPause);
        has_pause = true;
      }
    }
    if(h.has_metadata() && BTSession->isFilePreviewPossible(hash) && !has_preview) {
      listMenu.addAction(&actionPreview_file);
      has_preview = true;
    }
    if(has_pause && has_start && has_preview && one_not_seed) break;
  }
  listMenu.addSeparator();
  listMenu.addAction(&actionDelete);
  listMenu.addAction(&actionDelete_Permanently);
  listMenu.addSeparator();
  if(one_not_seed)
    listMenu.addAction(&actionSet_download_limit);
  listMenu.addAction(&actionSet_upload_limit);
  listMenu.addSeparator();
  if(one_has_metadata) {
    listMenu.addAction(&actionForce_recheck);
    listMenu.addSeparator();
  }
  listMenu.addAction(&actionOpen_destination_folder);
  if(BTSession->isQueueingEnabled() && one_not_seed) {
    listMenu.addSeparator();
    listMenu.addAction(&actionIncreasePriority);
    listMenu.addAction(&actionDecreasePriority);
  }
  listMenu.addSeparator();
  listMenu.addAction(&actionCopy_magnet_link);
  listMenu.addAction(&actionBuy_it);
  // Call menu
  listMenu.exec(QCursor::pos());
}

// Save columns width in a file to remember them
void TransferListWidget::saveColWidthList() {
  qDebug("Saving columns width in transfer list");
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QStringList width_list;
  QStringList new_width_list;
  short nbColumns = listModel->columnCount()-1;
  QString line = settings.value("TransferListColsWidth", QString()).toString();
  if(!line.isEmpty()) {
    width_list = line.split(' ');
  }
  for(short i=0; i<nbColumns; ++i){
    if(columnWidth(i)<1 && width_list.size() == nbColumns && width_list.at(i).toInt()>=1) {
      // load the former width
      new_width_list << width_list.at(i);
    } else if(columnWidth(i)>=1) {
      // usual case, save the current width
      new_width_list << misc::toQString(columnWidth(i));
    } else {
      // default width
      resizeColumnToContents(i);
      new_width_list << misc::toQString(columnWidth(i));
    }
  }
  settings.setValue(QString::fromUtf8("TransferListColsWidth"), new_width_list.join(QString::fromUtf8(" ")));
  QVariantList visualIndexes;
  for(int i=0; i<nbColumns; ++i) {
    visualIndexes.append(header()->visualIndex(i));
  }
  settings.setValue(QString::fromUtf8("TransferListVisualIndexes"), visualIndexes);
  qDebug("Download list columns width saved");
}

// Load columns width in a file that were saved previously
bool TransferListWidget::loadColWidthList() {
  qDebug("Loading columns width for download list");
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QString line = settings.value(QString::fromUtf8("TransferListColsWidth"), QString()).toString();
  if(line.isEmpty())
    return false;
  QStringList width_list = line.split(QString::fromUtf8(" "));
  if(width_list.size() != listModel->columnCount()-1) {
    qDebug("Corrupted values for download list columns sizes");
    return false;
  }
  unsigned int listSize = width_list.size();
  for(unsigned int i=0; i<listSize; ++i) {
    header()->resizeSection(i, width_list.at(i).toInt());
  }
  QVariantList visualIndexes = settings.value(QString::fromUtf8("TransferListVisualIndexes"), QVariantList()).toList();
  if(visualIndexes.size() != listModel->columnCount()-1) {
    qDebug("Corrupted values for download list columns sizes");
    return false;
  }
  bool change = false;
  do {
    change = false;
    for(int i=0;i<visualIndexes.size(); ++i) {
      int new_visual_index = visualIndexes.at(header()->logicalIndex(i)).toInt();
      if(i != new_visual_index) {
        qDebug("Moving column from %d to %d", header()->logicalIndex(i), new_visual_index);
        header()->moveSection(i, new_visual_index);
        change = true;
      }
    }
  }while(change);
  qDebug("Transfer list columns width loaded");
  return true;
}

void TransferListWidget::saveLastSortedColumn() {
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  Qt::SortOrder sortOrder = header()->sortIndicatorOrder();
  QString sortOrderLetter;
  if(sortOrder == Qt::AscendingOrder)
    sortOrderLetter = QString::fromUtf8("a");
  else
    sortOrderLetter = QString::fromUtf8("d");
  int index = header()->sortIndicatorSection();
  settings.setValue(QString::fromUtf8("TransferListSortedCol"), misc::toQString(index)+sortOrderLetter);
}

void TransferListWidget::loadLastSortedColumn() {
  // Loading last sorted column
  QSettings settings(QString::fromUtf8("qBittorrent"), QString::fromUtf8("qBittorrent"));
  QString sortedCol = settings.value(QString::fromUtf8("TransferListSortedCol"), QString()).toString();
  if(!sortedCol.isEmpty()) {
    Qt::SortOrder sortOrder;
    if(sortedCol.endsWith(QString::fromUtf8("d")))
      sortOrder = Qt::DescendingOrder;
    else
      sortOrder = Qt::AscendingOrder;
    sortedCol = sortedCol.left(sortedCol.size()-1);
    int index = sortedCol.toInt();
    sortByColumn(index, sortOrder);
  }
}
