#include "library/trackset/folders/folderstablemodel.h"

#include "library/dao/trackschema.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_folderstablemodel.cpp"
#include "util/db/fwdsqlquery.h"

namespace {

const QString kModelName = QStringLiteral("folders");

} // anonymous namespace

FoldersTableModel::FoldersTableModel(
        QObject* pParent,
        TrackCollectionManager* pTrackCollectionManager)
        : TrackSetTableModel(
                  pParent,
                  pTrackCollectionManager,
                  "mixxx.db.model.folders") {
}

void FoldersTableModel::selectFolder(const QString& directory) {
    m_selectedDirectory = directory;

    // Escape single quotes for the embedded literal below.
    QString escapedDir = directory;
    escapedDir.replace(QChar('\''), QStringLiteral("''"));

    // A stable per-directory temporary view. A view is a stored query, so it
    // always reflects the current tracks of that directory.
    const QString tableName = QStringLiteral("folder_%1")
                                      .arg(static_cast<qulonglong>(qHash(directory)));

    QStringList columns;
    columns << LIBRARYTABLE_ID
            << "'' AS " + LIBRARYTABLE_PREVIEW
            // For sorting the cover art column we give LIBRARYTABLE_COVERART
            // the same value as the cover digest.
            << LIBRARYTABLE_COVERART_DIGEST + " AS " + LIBRARYTABLE_COVERART;

    // Match the folder itself and any nested subfolder. The stored separator is
    // normalized to '/' so it works on Windows and *nix. GLOB '<p>/*' avoids
    // LIKE's '_'/'%' wildcard pitfalls in paths.
    const QString subselect =
            QStringLiteral(
                    "SELECT library.id FROM library "
                    "INNER JOIN track_locations "
                    "ON library.location = track_locations.id "
                    "WHERE REPLACE(track_locations.directory, '\\', '/') = '") +
            escapedDir +
            QStringLiteral(
                    "' OR REPLACE(track_locations.directory, '\\', '/') GLOB '") +
            escapedDir + QStringLiteral("/*'");

    QString queryString =
            QString("CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS "
                    "SELECT %2 FROM %3 "
                    "WHERE %4 IN (%5) "
                    "AND %6=0")
                    .arg(tableName,
                            columns.join(","),
                            LIBRARY_TABLE,
                            LIBRARYTABLE_ID,
                            subselect,
                            LIBRARYTABLE_MIXXXDELETED);
    FwdSqlQuery(m_database, queryString).execPrepared();

    columns[0] = LIBRARYTABLE_ID;
    columns[1] = LIBRARYTABLE_PREVIEW;
    columns[2] = LIBRARYTABLE_COVERART;
    setTable(tableName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());

    setSearch(QString());
    setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST),
            Qt::AscendingOrder);
}

TrackModel::Capabilities FoldersTableModel::getCapabilities() const {
    return Capability::AddToTrackSet |
            Capability::AddToAutoDJ |
            Capability::EditMetadata |
            Capability::LoadToDeck |
            Capability::LoadToSampler |
            Capability::LoadToPreviewDeck |
            Capability::ResetPlayed |
            Capability::Analyze |
            Capability::Properties |
            Capability::Sorting;
}

QString FoldersTableModel::modelKey(bool noSearch) const {
    if (noSearch) {
        return kModelName + QChar(':') + m_selectedDirectory;
    }
    return kModelName + QChar(':') + m_selectedDirectory +
            QChar('#') + currentSearch();
}
