#pragma once

#include "library/trackset/tracksettablemodel.h"

/// Read-only table model that lists the tracks of a single directory,
/// served from the library database (no disk access).
class FoldersTableModel final : public TrackSetTableModel {
    Q_OBJECT

  public:
    FoldersTableModel(QObject* parent, TrackCollectionManager* pTrackCollectionManager);
    ~FoldersTableModel() final = default;

    /// Show the tracks whose track_locations.directory == `directory`.
    void selectFolder(const QString& directory);
    const QString& selectedFolder() const {
        return m_selectedDirectory;
    }

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

  private:
    QString m_selectedDirectory;
};
