#include "library/trackset/folders/foldersfeature.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "moc_foldersfeature.cpp"
#include "util/assert.h"
#include "util/db/fwdsqlquery.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarytextbrowser.h"

FoldersFeature::FoldersFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary,
                  pConfig,
                  QStringLiteral("FOLDERSHOME"),
                  QStringLiteral("crates")),
          m_pTrackCollection(pLibrary->trackCollectionManager()->internalCollection()),
          m_foldersTableModel(this, pLibrary->trackCollectionManager()) {
    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    rebuildChildModel();
}

QVariant FoldersFeature::title() {
    return tr("Folders");
}

TreeItemModel* FoldersFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void FoldersFeature::bindLibraryWidget(
        WLibrary* libraryWidget, KeyboardEventFilter* keyboard) {
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(QStringLiteral("<h2>%1</h2><p>%2</p>")
                          .arg(tr("Folders"),
                                  tr("Browse your library by the folders on "
                                     "disk — fast, served from the database.")));
    edit->setOpenLinks(false);
    libraryWidget->registerView(m_rootViewName, edit);
}

void FoldersFeature::activateChild(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }
    TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    if (pItem == nullptr) {
        return;
    }
    const QString directory = pItem->getData().toString();
    if (directory.isEmpty()) {
        return;
    }
    emit saveModelState();
    m_foldersTableModel.selectFolder(directory);
    emit showTrackModel(&m_foldersTableModel);
    emit enableCoverArtDisplay(true);
}

void FoldersFeature::rebuildChildModel() {
    TreeItem* pRootItem = m_pSidebarModel->getRootItem();
    VERIFY_OR_DEBUG_ASSERT(pRootItem != nullptr) {
        return;
    }
    m_pSidebarModel->removeRows(0, pRootItem->childRows());

    // Collect the distinct directories of all library tracks.
    QStringList directories;
    FwdSqlQuery query(m_pTrackCollection->database(),
            QStringLiteral(
                    "SELECT DISTINCT track_locations.directory FROM track_locations "
                    "INNER JOIN library ON library.location = track_locations.id "
                    "WHERE library.mixxx_deleted = 0 "
                    "AND track_locations.directory IS NOT NULL "
                    "AND track_locations.directory != '' "
                    "ORDER BY track_locations.directory"));
    if (query.execPrepared()) {
        const int dirColumn = query.fieldIndex(QStringLiteral("directory"));
        while (query.next()) {
            directories.append(query.fieldValue(dirColumn).toString());
        }
    }

    // Build a nested tree from the path components. Each node stores its full
    // (normalized, '/'-separated) path as data so activateChild can filter.
    std::vector<std::unique_ptr<TreeItem>> topItems;
    QHash<QString, TreeItem*> nodeByPath;
    for (const QString& dir : directories) {
        QString normalized = dir;
        normalized.replace(QChar('\\'), QChar('/'));
        // Absolute *nix paths start with '/', which must be preserved in the
        // stored prefix so the filter matches the DB (e.g. "/Users/me/Music").
        const bool absolute = normalized.startsWith(QChar('/'));
        const QStringList segments =
                normalized.split(QChar('/'), Qt::SkipEmptyParts);
        TreeItem* pParent = nullptr;
        QString prefix;
        for (const QString& segment : segments) {
            if (prefix.isEmpty()) {
                prefix = absolute ? QChar('/') + segment : segment;
            } else {
                prefix += QChar('/') + segment;
            }
            TreeItem* pNode = nodeByPath.value(prefix, nullptr);
            if (pNode == nullptr) {
                if (pParent == nullptr) {
                    auto pNew = TreeItem::newRoot(this);
                    pNew->setLabel(segment);
                    pNew->setData(QVariant(prefix));
                    pNode = pNew.get();
                    topItems.push_back(std::move(pNew));
                } else {
                    pNode = pParent->appendChild(segment, QVariant(prefix));
                }
                nodeByPath.insert(prefix, pNode);
            }
            pParent = pNode;
        }
    }

    m_pSidebarModel->insertTreeItemRows(std::move(topItems), 0);
}
