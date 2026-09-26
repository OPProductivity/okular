/*
    SPDX-FileCopyrightText: 2021 Jiří Wolker <woljiri@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "recentitemsmodel.h"

#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QIcon>

#include <KConfigGroup>

#include <algorithm>

RecentItemsModel::RecentItemsModel()
{
}

RecentItemsModel::~RecentItemsModel()
{
}

void RecentItemsModel::loadEntries(const KConfigGroup &cg)
{
    beginResetModel();
    m_recentItems.clear();
    m_currentPage = 0;

    // Based on implementation of KRecentFilesAction::loadEntries.

    // read file list
    for (int i = 1; i <= maxItems(); i++) {
        const QString key = QStringLiteral("File%1").arg(i);
        const QString value = cg.readPathEntry(key, QString());
        if (value.isEmpty()) {
            continue;
        }
        const QUrl url = QUrl::fromUserInput(value);
        const QString nameKey = QStringLiteral("Name%1").arg(i);
        const QString nameValue = cg.readPathEntry(nameKey, url.fileName());
        m_recentItems.append(RecentItem {nameValue, url});
    }

    endResetModel();
}

void RecentItemsModel::clearEntries()
{
    beginResetModel();
    m_recentItems.clear();
    m_currentPage = 0;
    endResetModel();
}

QHash<int, QByteArray> RecentItemsModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();

    // Custom roles
    roles[IconNameRole] = "iconName";
    roles[UrlRole] = "url";

    return roles;
}

int RecentItemsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    return std::max(0, std::min(pageSize, static_cast<int>(m_recentItems.size()) - m_currentPage * pageSize));
}

QVariant RecentItemsModel::data(const QModelIndex &index, int role) const
{
    const RecentItemsModel::RecentItem *item = getItem(index);

    if (item != nullptr) {
        switch (role) {
        case Qt::ItemDataRole::DisplayRole:
            if (item->name.isEmpty()) {
                if (item->url.isLocalFile()) {
                    return item->url.toLocalFile();
                } else {
                    return item->url.toString();
                }
            } else {
                return item->name;
            }

        case Qt::ItemDataRole::ToolTipRole:
            if (item->url.isLocalFile()) {
                return item->url.toLocalFile();
            } else {
                return item->url.toString();
            }

        case Qt::ItemDataRole::DecorationRole:
            if (item->url.isLocalFile()) {
                return m_iconProvider.icon(QFileInfo(item->url.toLocalFile()));
            } else {
                // Fallback icon for remote files.
                return QIcon::fromTheme(QStringLiteral("network-server"));
            }

        case IconNameRole:
            if (item->url.isLocalFile()) {
                return m_iconProvider.icon(QFileInfo(item->url.toLocalFile())).name();
            } else {
                // Fallback icon for remote files.
                return QIcon::fromTheme(QStringLiteral("network-server")).name();
            }

        case UrlRole:
            return item->url.toString();

        default:
            return QVariant();
        }
    } else {
        return QVariant();
    }
}

RecentItemsModel::RecentItem const *RecentItemsModel::getItem(int index) const
{
    return index >= 0 && index < rowCount() ? getItemAtAbsoluteRow(m_currentPage * pageSize + index) : nullptr;
}

RecentItemsModel::RecentItem const *RecentItemsModel::getItemAtAbsoluteRow(int row) const
{
    return row >= 0 && row < m_recentItems.size() ? &m_recentItems[m_recentItems.size() - row - 1] : nullptr;
}

RecentItemsModel::RecentItem const *RecentItemsModel::getItem(const QModelIndex &index) const
{
    return getItem(index.row());
}

int RecentItemsModel::maxItems()
{
    return m_maxItems;
}

void RecentItemsModel::setMaxItems(const int maxItems)
{
    m_maxItems = maxItems;
}

int RecentItemsModel::totalItems() const
{
    return static_cast<int>(m_recentItems.size());
}

int RecentItemsModel::pageCount() const
{
    return (totalItems() + pageSize - 1) / pageSize;
}

int RecentItemsModel::currentPage() const
{
    return m_currentPage;
}

void RecentItemsModel::setPage(int page)
{
    if (page < 0 || page >= pageCount() || page == m_currentPage) {
        return;
    }
    beginResetModel();
    m_currentPage = page;
    endResetModel();
}

#include "moc_recentitemsmodel.cpp"
