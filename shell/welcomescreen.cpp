/*
    SPDX-FileCopyrightText: 2021 Jiří Wolker <woljiri@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "welcomescreen.h"

#include <KConfigGroup>
#include <KIO/OpenFileManagerWindowJob>
#include <KIconLoader>
#include <KMessageBox>
#include <KSharedConfig>

#include <QAction>
#include <QClipboard>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMap>
#include <QResizeEvent>
#include <QStyledItemDelegate>

#include "gui/recentitemsmodel.h"

#include <algorithm>

class RecentsListItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit RecentsListItemDelegate(WelcomeScreen *welcomeScreen)
        : m_welcomeScreen(welcomeScreen)
    {
    }

    WelcomeScreen *welcomeScreen() const
    {
        return m_welcomeScreen;
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *aModel, const QStyleOptionViewItem &styleOptionViewItem, const QModelIndex &index) override
    {
        const RecentItemsModel *model = static_cast<RecentItemsModel *>(aModel);
        const RecentItemsModel::RecentItem *item = model->getItem(index);

        QPoint menuPosition;

        if (item != nullptr) {
            bool willOpenMenu = false;
            if (event->type() == QEvent::ContextMenu) {
                willOpenMenu = true;
                menuPosition = static_cast<QContextMenuEvent *>(event)->globalPos();
            }
            if (event->type() == QEvent::MouseButtonPress) {
                if (static_cast<QMouseEvent *>(event)->button() == Qt::MouseButton::RightButton) {
                    willOpenMenu = true;
                    menuPosition = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
                }
            }

            if (willOpenMenu) {
                event->accept();

                const QList<QUrl> urls = welcomeScreen()->recentUrlsForContextMenu(index);
                QMenu menu;

                QAction *openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18n("&Open Selected"));
                connect(openAction, &QAction::triggered, this, [this, urls]() {
                    QList<QUrl> openingOrder = urls;
                    std::reverse(openingOrder.begin(), openingOrder.end());
                    Q_EMIT welcomeScreen()->openRecentDocuments(openingOrder);
                });
                menu.addSeparator();

                QAction *copyPathAction = new QAction(i18np("&Copy Path", "&Copy Paths", urls.size()));
                copyPathAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
                connect(copyPathAction, &QAction::triggered, this, [urls]() {
                    QStringList paths;
                    for (const QUrl &url : urls) {
                        paths.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
                    }
                    QGuiApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
                });
                menu.addAction(copyPathAction);

                QAction *showDirectoryAction = new QAction(i18np("&Open Containing Folder", "&Open Containing Folders", urls.size()));
                showDirectoryAction->setIcon(QIcon::fromTheme(QStringLiteral("document-open-folder")));
                connect(showDirectoryAction, &QAction::triggered, this, [this, urls]() {
                    QMap<QString, QList<QUrl>> filesByFolder;
                    for (const QUrl &url : urls) {
                        if (url.isLocalFile()) {
                            filesByFolder[QFileInfo(url.toLocalFile()).absolutePath()].append(url);
                        }
                    }
                    constexpr int maxFoldersPerAction = 5;
                    if (filesByFolder.size() > maxFoldersPerAction) {
                        KMessageBox::information(welcomeScreen(),
                                                 i18n("The selected files are in %1 different folders. Select files from at most %2 folders and try again.",
                                                      filesByFolder.size(),
                                                      maxFoldersPerAction));
                        return;
                    }
                    for (const QList<QUrl> &files : filesByFolder) {
                        KIO::highlightInFileManager(files);
                    }
                });
                menu.addAction(showDirectoryAction);
                if (std::none_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile(); })) {
                    showDirectoryAction->setEnabled(false);
                }

                QAction *forgetItemAction = new QAction(i18ncp("recent items context menu", "&Forget This Item", "&Forget These Items", urls.size()));
                forgetItemAction->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear-history")));
                connect(forgetItemAction, &QAction::triggered, this, [this, urls]() { Q_EMIT welcomeScreen()->forgetRecentItems(urls); });
                menu.addAction(forgetItemAction);

                menu.exec(menuPosition);

                return true;
            }
        }

        return QStyledItemDelegate::editorEvent(event, aModel, styleOptionViewItem, index);
    }

private:
    WelcomeScreen *m_welcomeScreen;
};

WelcomeScreen::WelcomeScreen(QWidget *parent)
    : QWidget(parent)
    , m_recentsModel(new RecentItemsModel)
    , m_recentsItemDelegate(new RecentsListItemDelegate(this))
{
    Q_ASSERT(parent);

    setupUi(this);

    connect(openButton, &QPushButton::clicked, this, &WelcomeScreen::openClicked);
    connect(closeButton, &QPushButton::clicked, this, &WelcomeScreen::closeClicked);

    recentsListView->setContextMenuPolicy(Qt::DefaultContextMenu);
    recentsListView->setModel(m_recentsModel);
    recentsListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    recentsListView->setItemDelegate(m_recentsItemDelegate);
    connect(recentsListView, &QListView::activated, this, &WelcomeScreen::recentsItemActivated);
    connect(recentsListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        openSelectedRecentsButton->setEnabled(recentsListView->selectionModel()->hasSelection());
    });

    m_recentPagesWidget = new QWidget(recentsArea);
    auto *pageLayout = new QHBoxLayout(m_recentPagesWidget);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(4);
    pageLayout->addStretch();
    m_previousRecentPageButton = new QToolButton(m_recentPagesWidget);
    m_previousRecentPageButton->setObjectName(QStringLiteral("previousRecentPageButton"));
    m_previousRecentPageButton->setArrowType(Qt::LeftArrow);
    m_previousRecentPageButton->setToolTip(i18n("Previous recent documents page"));
    pageLayout->addWidget(m_previousRecentPageButton);
    connect(m_previousRecentPageButton, &QToolButton::clicked, this, [this]() { m_recentsModel->setPage(m_recentsModel->currentPage() - 1); });
    for (int page = 0; page < 5; ++page) {
        auto *button = new QToolButton(m_recentPagesWidget);
        button->setObjectName(QStringLiteral("recentPageButton%1").arg(page + 1));
        button->setText(QString::number(page + 1));
        button->setCheckable(true);
        pageLayout->addWidget(button);
        m_recentPageButtons.append(button);
        connect(button, &QToolButton::clicked, this, [this, page]() { m_recentsModel->setPage(page); });
    }
    m_nextRecentPageButton = new QToolButton(m_recentPagesWidget);
    m_nextRecentPageButton->setObjectName(QStringLiteral("nextRecentPageButton"));
    m_nextRecentPageButton->setArrowType(Qt::RightArrow);
    m_nextRecentPageButton->setToolTip(i18n("Next recent documents page"));
    pageLayout->addWidget(m_nextRecentPageButton);
    connect(m_nextRecentPageButton, &QToolButton::clicked, this, [this]() { m_recentsModel->setPage(m_recentsModel->currentPage() + 1); });
    pageLayout->addStretch();
    recentsArea->layout()->addWidget(m_recentPagesWidget);
    m_recentPagesWidget->hide();

    connect(m_recentsModel, &RecentItemsModel::modelReset, this, &WelcomeScreen::recentListChanged);

    QVBoxLayout *noRecentsLayout = new QVBoxLayout(recentsListView);
    recentsListView->setLayout(noRecentsLayout);
    m_noRecentsLabel = new QLabel(recentsListView);
    QFont placeholderLabelFont;
    // To match the size of a level 2 Heading/KTitleWidget
    placeholderLabelFont.setPointSize(qRound(placeholderLabelFont.pointSize() * 1.3));
    noRecentsLayout->addWidget(m_noRecentsLabel);
    m_noRecentsLabel->setFont(placeholderLabelFont);
    m_noRecentsLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_noRecentsLabel->setWordWrap(true);
    m_noRecentsLabel->setAlignment(Qt::AlignCenter);
    m_noRecentsLabel->setText(i18nc("on welcome screen", "No recent documents"));
    // Match opacity of QML placeholder label component
    auto *effect = new QGraphicsOpacityEffect(m_noRecentsLabel);
    effect->setOpacity(0.5);
    m_noRecentsLabel->setGraphicsEffect(effect);

    connect(forgetAllButton, &QToolButton::clicked, this, &WelcomeScreen::forgetAllRecents);
    connect(openSelectedRecentsButton, &QToolButton::clicked, this, &WelcomeScreen::openSelectedRecentsClicked);
    connect(openAllRecentsButton, &QToolButton::clicked, this, &WelcomeScreen::openAllRecentsClicked);
}

WelcomeScreen::~WelcomeScreen()
{
    delete m_recentsModel;
    delete m_recentsItemDelegate;
}

void WelcomeScreen::showEvent(QShowEvent *e)
{
    if (appIcon->pixmap(Qt::ReturnByValue).isNull()) {
        appIcon->setPixmap(QIcon::fromTheme(QStringLiteral("okular")).pixmap(KIconLoader::SizeEnormous));
    }

    QWidget::showEvent(e);
}

void WelcomeScreen::loadRecents()
{
    recentsListView->clearSelection();
    m_recentsModel->loadEntries(KSharedConfig::openConfig()->group(QStringLiteral("Recent Files")));
}

void WelcomeScreen::setMaxRecentItems(const int maxItems)
{
    m_recentsModel->setMaxItems(maxItems);
}

int WelcomeScreen::recentsCount()
{
    return m_recentsModel->totalItems();
}

void WelcomeScreen::updateRecentPages()
{
    const int pages = m_recentsModel->pageCount();
    const int currentPage = m_recentsModel->currentPage();
    m_recentPagesWidget->setVisible(pages > 1);
    m_previousRecentPageButton->setEnabled(currentPage > 0);
    m_nextRecentPageButton->setEnabled(currentPage + 1 < pages);
    for (int page = 0; page < m_recentPageButtons.size(); ++page) {
        m_recentPageButtons.at(page)->setVisible(page < pages);
        m_recentPageButtons.at(page)->setChecked(page == currentPage);
    }
    recentsListView->scrollToTop();
}

QList<QUrl> WelcomeScreen::recentUrlsForContextMenu(const QModelIndex &clickedIndex)
{
    if (!recentsListView->selectionModel()->isSelected(clickedIndex)) {
        recentsListView->setCurrentIndex(clickedIndex);
        recentsListView->selectionModel()->select(clickedIndex, QItemSelectionModel::ClearAndSelect);
    }
    QList<QModelIndex> indexes;
    indexes = recentsListView->selectionModel()->selectedRows();
    std::sort(indexes.begin(), indexes.end(), [](const QModelIndex &a, const QModelIndex &b) {
        return a.row() < b.row();
    });

    QList<QUrl> urls;
    for (const QModelIndex &index : indexes) {
        const RecentItemsModel::RecentItem *item = m_recentsModel->getItem(index);
        if (item) {
            urls.append(item->url);
        }
    }
    return urls;
}

void WelcomeScreen::recentsItemActivated(const QModelIndex &index)
{
    const RecentItemsModel::RecentItem *item = m_recentsModel->getItem(index);
    if (item != nullptr) {
        Q_EMIT recentItemClicked(item->url);
    }
}

void WelcomeScreen::recentListChanged()
{
    updateRecentPages();
    openAllRecentsButton->setEnabled(recentsCount() > 0);
    openSelectedRecentsButton->setEnabled(recentsListView->selectionModel()->hasSelection());
    if (recentsCount() == 0) {
        m_noRecentsLabel->show();
        forgetAllButton->setEnabled(false);
    } else {
        m_noRecentsLabel->hide();
        forgetAllButton->setEnabled(true);
    }
}

void WelcomeScreen::openSelectedRecentsClicked()
{
    QList<QModelIndex> selectedRows = recentsListView->selectionModel()->selectedRows();
    std::sort(selectedRows.begin(), selectedRows.end(), [](const QModelIndex &a, const QModelIndex &b) {
        return a.row() > b.row();
    });

    QList<QUrl> urls;
    for (const QModelIndex &index : selectedRows) {
        const RecentItemsModel::RecentItem *item = m_recentsModel->getItem(index);
        if (item) {
            urls.append(item->url);
        }
    }
    if (!urls.isEmpty()) {
        Q_EMIT openRecentDocuments(urls);
    }
}

void WelcomeScreen::openAllRecentsClicked()
{
    QList<QUrl> urls;
    // The model displays the newest item first. Open it last so it remains active.
    for (int row = recentsCount() - 1; row >= 0; --row) {
        const RecentItemsModel::RecentItem *item = m_recentsModel->getItemAtAbsoluteRow(row);
        if (item) {
            urls.append(item->url);
        }
    }
    // Open All is independent of any Ctrl/Shift selection still in the list.
    recentsListView->clearSelection();
    Q_EMIT openRecentDocuments(urls);
}

#include "moc_welcomescreen.cpp"
#include "welcomescreen.moc"
