/*
 * Copyright (c) 2024 Meltytech, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "subtitlesdock.h"

#include "actions.h"
#include "mainwindow.h"
#include "settings.h"
#include "util.h"
#include "dialogs/subtitletrackdialog.h"
#include "models/subtitlesmodel.h"
#include "widgets/docktoolbar.h"
#include <Logger.h>

#include <QAction>
#include <QComboBox>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTextEdit>
#include <QTreeView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtWidgets/QScrollArea>

#define DEFAULT_ITEM_DURATION (2 * 1000)

static int64_t positionToMs(mlt_position position)
{
    return position * 1000 * MLT.profile().frame_rate_den() / MLT.profile().frame_rate_num();
}

static mlt_position msToPosition(int64_t ms)
{
    return ms * MLT.profile().frame_rate_num() / MLT.profile().frame_rate_den() / 1000;
}

SubtitlesDock::SubtitlesDock(QWidget *parent) :
    QDockWidget(parent)
    , m_model(nullptr)
    , m_pos(-1)
    , m_textEditInProgress(false)
{
    LOG_DEBUG() << "begin";

    setObjectName("SubtitlesDock");
    QDockWidget::setWindowTitle(tr("Subtitles"));
    QIcon filterIcon = QIcon::fromTheme("subtitle", QIcon(":/icons/oxygen/32x32/actions/subtitle.png"));
    setWindowIcon(filterIcon);
    toggleViewAction()->setIcon(windowIcon());

    setupActions();

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    QDockWidget::setWidget(scrollArea);

    QVBoxLayout *vboxLayout = new QVBoxLayout();
    scrollArea->setLayout(vboxLayout);

    QHBoxLayout *tracksLayout = new QHBoxLayout();
    m_trackCombo = new QComboBox();
    connect(m_trackCombo, &QComboBox::currentIndexChanged, this, [&](int trackIndex) {
        if (m_model && m_treeView && trackIndex >= 0) {
            QModelIndex modelIndex = m_model->trackModelIndex(trackIndex);
            m_treeView->setRootIndex(modelIndex);
            selectItemForTime();
        }
    });
    tracksLayout->addWidget(m_trackCombo);
    QToolButton *addTrackButton = new QToolButton(this);
    addTrackButton->setDefaultAction(Actions["subtitleAddTrackAction"]);
    addTrackButton->setAutoRaise(true);
    tracksLayout->addWidget(addTrackButton);
    QToolButton *removeTrackButton = new QToolButton(this);
    removeTrackButton->setDefaultAction(Actions["subtitleRemoveTrackAction"]);
    removeTrackButton->setAutoRaise(true);
    tracksLayout->addWidget(removeTrackButton);

    vboxLayout->addLayout(tracksLayout);

    m_treeView = new QTreeView();
    vboxLayout->addWidget(m_treeView, 1);

    QMenu *mainMenu = new QMenu("Subtitles", this);
    QMenu *trackMenu = new QMenu("Tracks", this);
    trackMenu->addAction(Actions["subtitleAddTrackAction"]);
    trackMenu->addAction(Actions["subtitleRemoveTrackAction"]);
    mainMenu->addMenu(trackMenu);
    mainMenu->addAction(Actions["SubtitleImportAction"]);
    mainMenu->addAction(Actions["SubtitleExportAction"]);
    mainMenu->addAction(Actions["subtitleOverwriteItemAction"]);
    mainMenu->addAction(Actions["subtitleAppendItemAction"]);
    mainMenu->addAction(Actions["subtitleRemoveItemAction"]);
    mainMenu->addAction(Actions["subtitleSetStartAction"]);
    mainMenu->addAction(Actions["subtitleSetEndAction"]);
    mainMenu->addAction(Actions["subtitleMoveAction"]);
    mainMenu->addAction(Actions["subtitleTrackTimelineAction"]);
    mainMenu->addAction(Actions["subtitleShowPrevNextAction"]);

    QAction *action;
    QMenu *columnsMenu = new QMenu(tr("Columns"), this);
    action = columnsMenu->addAction(tr("Columns"));
    action->setEnabled(false);
    action = columnsMenu->addAction(tr("Start"), this, SLOT(onStartColumnToggled(bool)));
    action->setCheckable(true);
    action->setChecked(Settings.subtitlesShowColumn("start"));
    action = columnsMenu->addAction(tr("End"), this, SLOT(onEndColumnToggled(bool)));
    action->setCheckable(true);
    action->setChecked(Settings.subtitlesShowColumn("end"));
    action = columnsMenu->addAction(tr("Duration"), this, SLOT(onDurationColumnToggled(bool)));
    action->setCheckable(true);
    action->setChecked(Settings.subtitlesShowColumn("duration"));
    mainMenu->addMenu(columnsMenu);
    Actions.loadFromMenu(columnsMenu);

    DockToolBar *toolbar = new DockToolBar(tr("Subtitle Controls"));
    toolbar->setAreaHint(Qt::BottomToolBarArea);
    QToolButton *menuButton = new QToolButton(this);
    menuButton->setIcon(QIcon::fromTheme("show-menu",
                                         QIcon(":/icons/oxygen/32x32/actions/show-menu.png")));
    menuButton->setToolTip(tr("Subtitles Menu"));
    menuButton->setAutoRaise(true);
    menuButton->setMenu(mainMenu);
    menuButton->setPopupMode(QToolButton::QToolButton::InstantPopup);
    toolbar->addWidget(menuButton);

    QToolButton *importItemsButton = new QToolButton(this);
    importItemsButton->setDefaultAction(Actions["SubtitleImportAction"]);
    importItemsButton->setAutoRaise(true);
    toolbar->addWidget(importItemsButton);

    QToolButton *overwriteButton = new QToolButton(this);
    overwriteButton->setDefaultAction(Actions["subtitleOverwriteItemAction"]);
    overwriteButton->setAutoRaise(true);
    toolbar->addWidget(overwriteButton);

    QToolButton *addButton = new QToolButton(this);
    addButton->setDefaultAction(Actions["subtitleAppendItemAction"]);
    addButton->setAutoRaise(true);
    toolbar->addWidget(addButton);

    QToolButton *removeButton = new QToolButton(this);
    removeButton->setDefaultAction(Actions["subtitleRemoveItemAction"]);
    removeButton->setAutoRaise(true);
    toolbar->addWidget(removeButton);

    QToolButton *setStartButton = new QToolButton(this);
    setStartButton->setDefaultAction(Actions["subtitleSetStartAction"]);
    removeButton->setAutoRaise(true);
    toolbar->addWidget(setStartButton);

    QToolButton *setEndButton = new QToolButton(this);
    setEndButton->setDefaultAction(Actions["subtitleSetEndAction"]);
    setEndButton->setAutoRaise(true);
    toolbar->addWidget(setEndButton);

    QToolButton *moveButton = new QToolButton(this);
    moveButton->setDefaultAction(Actions["subtitleMoveAction"]);
    moveButton->setAutoRaise(true);
    toolbar->addWidget(moveButton);

    vboxLayout->addWidget(toolbar);

    QFontMetrics fm(font());
    int textHeight = fm.lineSpacing() * 4 + 10;
    QGridLayout *textLayout = new QGridLayout();
    m_prevLabel = new QLabel(tr("Previous"));
    textLayout->addWidget(m_prevLabel, 0, 0);
    m_textLabel = new QLabel(tr("Current"));
    textLayout->addWidget(m_textLabel, 0, 1);
    m_nextLabel = new QLabel(tr("Next"));
    textLayout->addWidget(m_nextLabel, 0, 2);
    m_prev = new QTextEdit(this);
    m_prev->setMaximumHeight(textHeight);
    m_prev->setReadOnly(true);
    m_prev->setLineWrapMode(QTextEdit::NoWrap);
    textLayout->addWidget(m_prev, 1, 0);
    m_text = new QTextEdit(this);
    m_text->setMaximumHeight(textHeight);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QTextEdit::NoWrap);
    connect(m_text, &QTextEdit::textChanged, this, &SubtitlesDock::onTextEdited);
    textLayout->addWidget(m_text, 1, 1);
    m_next = new QTextEdit(this);
    m_next->setMaximumHeight(textHeight);
    m_next->setReadOnly(true);
    m_next->setLineWrapMode(QTextEdit::NoWrap);
    textLayout->addWidget(m_next, 1, 2);
    vboxLayout->addLayout(textLayout);

    vboxLayout->addStretch();

    LOG_DEBUG() << "end";
}

SubtitlesDock::~SubtitlesDock()
{
}

void SubtitlesDock::setupActions()
{
    QAction *action;

    action = new QAction(tr("Add Subtitle Track"), this);
    action->setIcon(QIcon::fromTheme("list-add",
                                     QIcon(":/icons/oxygen/32x32/actions/list-add.png")));
    action->setToolTip(tr("Add a subtitle track"));
    connect(action, &QAction::triggered, this, [&]() {
        show();
        raise();
        addSubtitleTrack();
    });
    Actions.add("subtitleAddTrackAction", action);

    action = new QAction(tr("Remove Subtitle Track"), this);
    action->setIcon(QIcon::fromTheme("list-remove",
                                     QIcon(":/icons/oxygen/32x32/actions/list-remove.png")));
    action->setToolTip(tr("Remove this subtitle track"));
    connect(action, &QAction::triggered, this, [&]() {
        show();
        raise();
        removeSubtitleTrack();
    });
    Actions.add("subtitleRemoveTrackAction", action);

    action = new QAction(tr("Import Subtitles From File"), this);
    action->setIcon(QIcon::fromTheme("document-import",
                                     QIcon(":/icons/oxygen/32x32/actions/document-import.png")));
    action->setToolTip(tr("Import subtitles from an srt file at the current position"));
    connect(action, &QAction::triggered, this, [&]() {
        show();
        raise();
        importSubtitles();
    });
    Actions.add("SubtitleImportAction", action);

    action = new QAction(tr("Export Subtitles To File"), this);
    action->setIcon(QIcon::fromTheme("document-export",
                                     QIcon(":/icons/oxygen/32x32/actions/document-export.png")));
    action->setToolTip(tr("Export the current subtitle track to an SRT file"));
    connect(action, &QAction::triggered, this, [&]() {
        show();
        raise();
        exportSubtitles();
    });
    Actions.add("SubtitleExportAction", action);

    action = new QAction(tr("Append Subtitle Item"), this);
    action->setIcon(QIcon::fromTheme("list-add",
                                     QIcon(":/icons/oxygen/32x32/actions/list-add.png")));
    action->setToolTip(tr("Append a subtitle to the end"));
    connect(action, &QAction::triggered, this, &SubtitlesDock::onAppendRequested);
    Actions.add("subtitleAppendItemAction", action, windowTitle());

    action = new QAction(tr("Overwrite Subtitle Item"), this);
    action->setIcon(QIcon::fromTheme("overwrite",
                                     QIcon(":/icons/oxygen/32x32/actions/overwrite.png")));
    action->setToolTip(tr("Overwrite subtitle at the cursor position."));
    connect(action, &QAction::triggered, this, &SubtitlesDock::onOverwriteRequested);
    Actions.add("subtitleOverwriteItemAction", action, windowTitle());

    action = new QAction(tr("Remove Subtitle Item"), this);
    action->setIcon(QIcon::fromTheme("list-remove",
                                     QIcon(":/icons/oxygen/32x32/actions/list-remove.png")));
    action->setToolTip(tr("Remove the selected subtitle item"));
    connect(action, &QAction::triggered, this, &SubtitlesDock::onRemoveRequested);
    Actions.add("subtitleRemoveItemAction", action, windowTitle());

    action = new QAction(tr("Set Subtitle Start"), this);
    action->setIcon(QIcon::fromTheme("keyframes-filter-in",
                                     QIcon(":/icons/oxygen/32x32/actions/keyframes-filter-in.png")));
    action->setToolTip(tr("Set the selected subtitle to start at the cursor position"));
    connect(action, &QAction::triggered, this, &SubtitlesDock::onSetStartRequested);
    Actions.add("subtitleSetStartAction", action, windowTitle());

    action = new QAction(tr("Set Subtitle End"), this);
    action->setIcon(QIcon::fromTheme("keyframes-filter-out",
                                     QIcon(":/icons/oxygen/32x32/actions/keyframes-filter-out.png")));
    action->setToolTip(tr("Set the selected subtitle to end at the cursor position"));
    connect(action, &QAction::triggered, this, &SubtitlesDock::onSetEndRequested);
    Actions.add("subtitleSetEndAction", action, windowTitle());

    action = new QAction(tr("Move Subtitles"), this);
    action->setIcon(QIcon::fromTheme("4-direction",
                                     QIcon(":/icons/oxygen/32x32/actions/4-direction.png")));
    action->setToolTip(tr("Move the selected subtitles to the cursor position"));
    connect(action, &QAction::triggered, this, &SubtitlesDock::onMoveRequested);
    Actions.add("subtitleMoveAction", action, windowTitle());

    action = new QAction(tr("Track Timeline Cursor"), this);
    action->setToolTip(tr("Track the timeline cursor"));
    action->setCheckable(true);
    action->setChecked(Settings.subtitlesTrackTimeline());
    connect(action, &QAction::toggled, this, [&](bool checked) {
        Settings.setSubtitlesTrackTimeline(checked);
    });
    Actions.add("subtitleTrackTimelineAction", action, windowTitle());

    action = new QAction(tr("Show Previous/Next"), this);
    action->setToolTip(tr("Show the previous and next subtitles"));
    action->setCheckable(true);
    action->setChecked(Settings.subtitlesShowPrevNext());
    connect(action, &QAction::toggled, this, [&](bool checked) {
        Settings.setSubtitlesShowPrevNext(checked);
        resizeTextWidgets();
    });
    Actions.add("subtitleShowPrevNextAction", action, windowTitle());
}

void SubtitlesDock::setModel(SubtitlesModel *model)
{
    m_model = model;
    m_treeView->setModel(m_model);
    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_treeView->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setColumnHidden(2, !Settings.subtitlesShowColumn("start"));
    m_treeView->setColumnHidden(3, !Settings.subtitlesShowColumn("end"));
    m_treeView->setColumnHidden(4, !Settings.subtitlesShowColumn("duration"));
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &SubtitlesDock::refreshWidgets);
    connect(m_treeView, &QTreeView::doubleClicked, this, &SubtitlesDock::onItemDoubleClicked);
    connect(m_model, &QAbstractItemModel::modelReset, this, &SubtitlesDock::onModelReset);
    connect(m_model, &SubtitlesModel::tracksChanged, this, &SubtitlesDock::refreshTracksCombo);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &SubtitlesDock::refreshWidgets);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &SubtitlesDock::refreshWidgets);
    connect(m_model, &QAbstractItemModel::rowsMoved, this, &SubtitlesDock::refreshWidgets);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [&](const QModelIndex & parent, int first,
    int last) {
        if (parent.isValid()) {
            refreshWidgets();
        }
    });
    refreshTracksCombo();
    refreshWidgets();
}

void SubtitlesDock::addSubtitleTrack()
{
    int newIndex = m_model->trackCount();
    if (!MAIN.isMultitrackValid()) {
        MAIN.showStatusMessage(tr("Add a clip to the timeline to create subtitles."));
        return;
    }
    int i = 1;
    QString suggestedName = tr("Subtitle Track %1").arg(i);
    while (trackNameExists(suggestedName)) {
        suggestedName = tr("Subtitle Track %1").arg(i++);
    }
    SubtitleTrackDialog dialog(suggestedName, this);
    if (dialog.exec() == QDialog::Accepted) {
        SubtitlesModel::SubtitleTrack track;
        track.name = dialog.getName();
        track.lang = dialog.getLanguage();
        if (trackNameExists(track.name)) {
            MAIN.showStatusMessage(tr("Subtitle track already exists: %1").arg(track.name));
            return;
        }
        m_model->addTrack(track);
    }
    m_trackCombo->setCurrentIndex(newIndex);
}

void SubtitlesDock::removeSubtitleTrack()
{
    QString name = m_trackCombo->currentData().toString();
    if (!name.isEmpty()) {
        m_model->removeTrack(name);
    }
}

void SubtitlesDock::importSubtitles()
{
    QString srtPath = QFileDialog::getOpenFileName(&MAIN, tr("Import SRT File"), Settings.openPath(),
                                                   tr("SRT Files (*.srt *.SRT)"),
                                                   nullptr, Util::getFileDialogOptions());
    if (srtPath.isEmpty()) {
        return;
    }
    if (!QFileInfo(srtPath).exists()) {
        MAIN.showStatusMessage(tr("Unable to find srt file."));
        return;
    }

    m_model->importSubtitles(srtPath, m_trackCombo->currentIndex(), positionToMs(m_pos));
}

void SubtitlesDock::exportSubtitles()
{
    auto track = m_model->getTrack(m_trackCombo->currentIndex());
    QString suggestedPath = QString("%1/%2.srt").arg(Settings.savePath()).arg(track.name);
    QString srtPath = QFileDialog::getSaveFileName(&MAIN, tr("Export SRT File"), suggestedPath,
                                                   tr("SRT Files (*.srt *.SRT)"),
                                                   nullptr, Util::getFileDialogOptions());
    if (srtPath.isEmpty()) {
        return;
    }

    m_model->exportSubtitles(srtPath, m_trackCombo->currentIndex());
    Settings.setSavePath(srtPath);
}

void SubtitlesDock::onPositionChanged(int position)
{
    if (position == m_pos) {
        return;
    }
    m_pos = position;
    if (Actions["subtitleTrackTimelineAction"]->isChecked()) {
        selectItemForTime();
    }
}

void SubtitlesDock::refreshTracksCombo()
{
    if (m_model) {
        QList<SubtitlesModel::SubtitleTrack> tracks = m_model->getTracks();
        LOG_DEBUG() << "tracks" << tracks.size();
        m_trackCombo->clear();
        for (auto &track : tracks) {
            m_trackCombo->addItem(QString("%1 (%2)").arg(track.name).arg(track.lang), track.name);
        }
        selectItemForTime();
    }
}

void SubtitlesDock::onAppendRequested()
{
    LOG_DEBUG();
    int trackIndex = m_trackCombo->currentIndex();
    int count = m_model->itemCount(trackIndex);
    int64_t appendTime = m_model->endTime(trackIndex);
    if ((m_model->maxTime() - appendTime) < 500) {
        MAIN.showStatusMessage(tr("Unable to append subtitles"));
        return;
    }
    Subtitles::SubtitleItem item;
    item.start = appendTime;
    item.end = appendTime + DEFAULT_ITEM_DURATION;
    if (item.end > m_model->maxTime()) {
        item.end = m_model->maxTime() - appendTime;
    }
    m_model->overwriteItem(trackIndex, item);
    setCurrentItem(trackIndex, count);
}

void SubtitlesDock::onOverwriteRequested()
{
    LOG_DEBUG();
    int64_t msTime = positionToMs(m_pos);
    int trackIndex = m_trackCombo->currentIndex();
    int newItemIndex = m_model->itemIndexAtTime(trackIndex, msTime);
    if (newItemIndex < 0) {
        newItemIndex = m_model->itemIndexBeforeTime(trackIndex, msTime) + 1;
    }
    Subtitles::SubtitleItem item;
    item.start = msTime;
    item.end = msTime + DEFAULT_ITEM_DURATION;
    m_model->overwriteItem(trackIndex, item);
    setCurrentItem(trackIndex, newItemIndex);
}

void SubtitlesDock::onRemoveRequested()
{
    LOG_DEBUG();
    int trackIndex = m_trackCombo->currentIndex();
    QModelIndexList selectedRows = m_treeView->selectionModel()->selectedRows();
    if (selectedRows.size() > 0) {
        int firstItemIndex = selectedRows[0].row();
        int lastItemIndex = selectedRows[selectedRows.size() - 1].row();
        m_model->removeItems(trackIndex, firstItemIndex, lastItemIndex);
    }
}

void SubtitlesDock::onSetStartRequested()
{
    LOG_DEBUG();
    int trackIndex = m_trackCombo->currentIndex();
    QModelIndexList selectedRows = m_treeView->selectionModel()->selectedRows();
    if (selectedRows.size() > 0) {
        int itemIndex = selectedRows[0].row();
        int64_t msTime = positionToMs(m_pos);
        auto currentItem = m_model->getItem(trackIndex, itemIndex);
        if (msTime >= currentItem.end) {
            MAIN.showStatusMessage(tr("Start time can not be after end time."));
            return;
        }
        if (itemIndex > 0) {
            auto prevItem = m_model->getItem(trackIndex, itemIndex - 1);
            if (msTime < prevItem.end) {
                MAIN.showStatusMessage(tr("Start time can not be before previous subtitle."));
                return;
            }
        }
        m_model->setItemStart(trackIndex, itemIndex, msTime);
    }
}

void SubtitlesDock::onSetEndRequested()
{
    LOG_DEBUG();
    int trackIndex = m_trackCombo->currentIndex();
    QModelIndexList selectedRows = m_treeView->selectionModel()->selectedRows();
    if (selectedRows.size() > 0) {
        int itemIndex = selectedRows[0].row();
        int64_t msTime = positionToMs(m_pos);
        auto currentItem = m_model->getItem(trackIndex, itemIndex);
        if (msTime <= currentItem.start) {
            MAIN.showStatusMessage(tr("End time can not be before start time."));
            return;
        }
        int itemCount = m_model->itemCount(trackIndex);
        if (itemIndex < (itemCount - 1)) {
            auto nextItem = m_model->getItem(trackIndex, itemIndex + 1);
            if (msTime > nextItem.start) {
                MAIN.showStatusMessage(tr("End time can not be after next subtitle."));
                return;
            }
        }
        m_model->setItemEnd(trackIndex, itemIndex, msTime);
    }
}

void SubtitlesDock::onMoveRequested()
{
    QModelIndexList selectedRows = m_treeView->selectionModel()->selectedRows();
    if (selectedRows.size() <= 0) {
        return;
    }
    int trackIndex = m_trackCombo->currentIndex();
    int64_t msTime = positionToMs(m_pos);
    // Check if there is a big enough gap at this location to move without conflict.
    int firstItemIndex = selectedRows[0].row();
    auto firstItem = m_model->getItem(trackIndex, firstItemIndex);
    int lastItemIndex = selectedRows[selectedRows.size() - 1].row();
    auto lastItem = m_model->getItem(trackIndex, lastItemIndex);
    int64_t duration = lastItem.end - firstItem.start;
    int64_t newEndTime = msTime + duration;
    int itemCount = m_model->itemCount(trackIndex);

    int gapItemIndex = m_model->itemIndexAtTime(trackIndex, msTime);
    if (gapItemIndex == - 1) {
        gapItemIndex = m_model->itemIndexAfterTime(trackIndex, msTime);
    }
    if (gapItemIndex >= 0) {
        while (gapItemIndex < itemCount) {
            if (gapItemIndex < firstItemIndex || gapItemIndex > lastItemIndex) {
                auto gapItem = m_model->getItem(trackIndex, gapItemIndex);
                if (gapItem.start >= newEndTime) {
                    break;
                } else if ((msTime >= gapItem.start && msTime < gapItem.end) ||
                           (newEndTime > gapItem.start && newEndTime <= gapItem.end) ||
                           (gapItem.start <= msTime && gapItem.end >= newEndTime)) {
                    // This existing item will cause a conflict.
                    MAIN.showStatusMessage(tr("Unable to move. Subtitles already exist at this time."));
                    return;
                }
            }
            gapItemIndex++;
        }
    }
    m_model->moveItems(trackIndex, firstItemIndex, lastItemIndex, msTime);
}

void SubtitlesDock::onTextEdited()
{
    LOG_DEBUG();
    QModelIndex currentIndex = m_treeView->selectionModel()->currentIndex();
    if (currentIndex.isValid() && currentIndex.parent().isValid()) {
        m_textEditInProgress = true;
        m_model->setText(currentIndex.parent().row(), currentIndex.row(), m_text->toPlainText());
        m_textEditInProgress = false;
    } else {
        LOG_ERROR() << "Invalid index" << currentIndex;
    }
}

void SubtitlesDock::onModelReset()
{
    refreshTracksCombo();
    refreshWidgets();
}

void SubtitlesDock::onStartColumnToggled(bool checked)
{
    Settings.setSubtitlesShowColumn("start", checked);
    m_treeView->setColumnHidden(1, !checked);
}

void SubtitlesDock::onEndColumnToggled(bool checked)
{
    Settings.setSubtitlesShowColumn("end", checked);
    m_treeView->setColumnHidden(2, !checked);
}

void SubtitlesDock::onDurationColumnToggled(bool checked)
{
    Settings.setSubtitlesShowColumn("duration", checked);
    m_treeView->setColumnHidden(3, !checked);
}

void SubtitlesDock::onItemDoubleClicked(const QModelIndex &index)
{
    if (index.parent().isValid()) {
        const Subtitles::SubtitleItem item = m_model->getItem(index.parent().row(), index.row());
        int64_t position = msToPosition(item.start);
        emit seekRequested((int)position);
    }
}

void SubtitlesDock::resizeEvent(QResizeEvent *e)
{
    QDockWidget::resizeEvent(e);
    resizeTextWidgets();
}

void SubtitlesDock::resizeTextWidgets()
{
    if (Settings.subtitlesShowPrevNext()) {
        m_prev->setVisible(true);
        m_prev->setMinimumSize(QSize(width() / 3, m_text->maximumHeight()));
        m_text->setMinimumSize(QSize(width() / 3, m_text->maximumHeight()));
        m_next->setVisible(true);
        m_next->setMinimumSize(QSize(width() / 3, m_text->maximumHeight()));
        m_prevLabel->setVisible(true);
        m_textLabel->setVisible(true);
        m_nextLabel->setVisible(true);
    } else {
        m_text->setMinimumSize(QSize(width(), m_text->maximumHeight()));
        m_prev->setVisible(false);
        m_next->setVisible(false);
        m_prevLabel->setVisible(false);
        m_textLabel->setVisible(false);
        m_nextLabel->setVisible(false);
    }
}

void SubtitlesDock::updateTextWidgets()
{
    Subtitles::SubtitleItem item;
    const QSignalBlocker blocker(m_text);

    // First update based on current (selected) item.
    QModelIndex currentIndex = m_treeView->selectionModel()->currentIndex();
    if (currentIndex.isValid() && currentIndex.parent().isValid()) {
        int trackIndex = currentIndex.parent().row();
        int itemIndex = currentIndex.row();
        if (trackIndex >= 0 && trackIndex < m_model->trackCount() && itemIndex >= 0
                && itemIndex < m_model->itemCount(trackIndex)) {
            item = m_model->getItem(trackIndex, itemIndex);
            m_text->setPlainText(QString::fromStdString(item.text));
            m_text->setReadOnly(false);
            if (itemIndex > 0) {
                item = m_model->getItem(trackIndex, itemIndex - 1);
                m_prev->setPlaceholderText(QString::fromStdString(item.text));
            } else {
                m_prev->setPlaceholderText(QString());
            }
            if (itemIndex < (m_model->itemCount(trackIndex) - 1)) {
                item = m_model->getItem(trackIndex, itemIndex + 1);
                m_next->setPlaceholderText(QString::fromStdString(item.text));
            } else {
                m_next->setPlaceholderText(QString());
            }
            return;
        } else {
            LOG_ERROR() << "Invalid row selected" << currentIndex;
        }
    }
    // If nothing selected, update based on position (but not editable)
    m_text->setReadOnly(true);
    int trackIndex = m_trackCombo->currentIndex();
    if (trackIndex >= 0 && m_pos >= 0) {
        int64_t msTime = positionToMs(m_pos);
        int itemIndex = m_model->itemIndexAtTime(trackIndex, msTime);
        if (itemIndex >= 0) {
            item = m_model->getItem(trackIndex, itemIndex);
            m_text->setPlainText(QString::fromStdString(item.text));
            m_text->setReadOnly(false);
            if (itemIndex > 0) {
                item = m_model->getItem(trackIndex, itemIndex - 1);
                m_prev->setPlaceholderText(QString::fromStdString(item.text));
            } else {
                m_prev->setPlaceholderText(QString());
            }
            if (itemIndex < (m_model->itemCount(trackIndex) - 1)) {
                item = m_model->getItem(trackIndex, itemIndex + 1);
                m_next->setPlaceholderText(QString::fromStdString(item.text));
            } else {
                m_next->setPlaceholderText(QString());
            }
        } else {
            m_text->clear();
            itemIndex = m_model->itemIndexBeforeTime(trackIndex, msTime);
            if (itemIndex >= 0) {
                item = m_model->getItem(trackIndex, itemIndex);
                m_prev->setPlaceholderText(QString::fromStdString(item.text));
            } else {
                m_prev->setPlaceholderText(QString());
            }
            itemIndex = m_model->itemIndexAfterTime(trackIndex, msTime);
            if (itemIndex >= 0) {
                item = m_model->getItem(trackIndex, itemIndex);
                m_next->setPlaceholderText(QString::fromStdString(item.text));
            } else {
                m_next->setPlaceholderText(QString());
            }
        }
    } else {
        m_text->setReadOnly(true);
        m_text->clear();
        m_prev->setPlaceholderText(QString());
        m_next->setPlaceholderText(QString());
    }
}

void SubtitlesDock::updateActionAvailablity()
{
    if (!m_model || !m_model->isValid()) {
        // Disable all actions
        Actions["subtitleAddTrackAction"]->setEnabled(false);
        Actions["subtitleRemoveTrackAction"]->setEnabled(false);
        Actions["SubtitleImportAction"]->setEnabled(false);
        Actions["SubtitleExportAction"]->setEnabled(false);
        Actions["subtitleAppendItemAction"]->setEnabled(false);
        Actions["subtitleRemoveItemAction"]->setEnabled(false);
        Actions["subtitleSetStartAction"]->setEnabled(false);
        Actions["subtitleSetEndAction"]->setEnabled(false);
    } else {
        Actions["subtitleAddTrackAction"]->setEnabled(true);
        if (m_model->trackCount() == 0) {
            Actions["subtitleRemoveTrackAction"]->setEnabled(false);
            Actions["SubtitleImportAction"]->setEnabled(false);
            Actions["SubtitleExportAction"]->setEnabled(false);
            Actions["subtitleAppendItemAction"]->setEnabled(false);
            Actions["subtitleRemoveItemAction"]->setEnabled(false);
            Actions["subtitleSetStartAction"]->setEnabled(false);
            Actions["subtitleSetEndAction"]->setEnabled(false);
        } else {
            Actions["subtitleRemoveTrackAction"]->setEnabled(true);
            Actions["SubtitleImportAction"]->setEnabled(true);
            Actions["SubtitleExportAction"]->setEnabled(true);
            Actions["subtitleAppendItemAction"]->setEnabled(true);
            if (m_treeView->selectionModel()->selectedRows().size() == 1) {
                Actions["subtitleSetStartAction"]->setEnabled(true);
                Actions["subtitleSetEndAction"]->setEnabled(true);
            } else {
                Actions["subtitleSetStartAction"]->setEnabled(false);
                Actions["subtitleSetEndAction"]->setEnabled(false);
            }
            if (m_treeView->selectionModel()->selectedRows().size() > 0) {
                Actions["subtitleRemoveItemAction"]->setEnabled(true);
            } else {
                Actions["subtitleRemoveItemAction"]->setEnabled(false);
            }
        }
    }
}

void SubtitlesDock::refreshWidgets()
{
    if (m_textEditInProgress) {
        return;
    }
    updateTextWidgets();
    updateActionAvailablity();
}

void SubtitlesDock::setCurrentItem(int trackIndex, int itemIndex)
{
    if (itemIndex >= 0) {
        QModelIndex modelIndex = m_model->itemModelIndex(trackIndex, itemIndex);
        if (modelIndex.isValid()) {
            m_treeView->setCurrentIndex(modelIndex);
            return;
        }
    }
    m_treeView->setCurrentIndex(QModelIndex());
}

void SubtitlesDock::selectItemForTime()
{
    int trackIndex = m_trackCombo->currentIndex();
    if (trackIndex >= 0) {
        int64_t time = positionToMs(m_pos);
        int itemIndex = m_model->itemIndexAtTime(trackIndex, time);
        setCurrentItem(trackIndex, itemIndex);
    }
}

bool SubtitlesDock::trackNameExists(const QString &name)
{
    QList<SubtitlesModel::SubtitleTrack> tracks = m_model->getTracks();
    for (auto &track : tracks) {
        if (track.name == name) {
            return true;
        }
    }
    return false;
}
