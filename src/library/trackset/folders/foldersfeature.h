#pragma once

#include <QModelIndex>
#include <QVariant>

#include "library/trackset/basetracksetfeature.h"
#include "library/trackset/folders/folderstablemodel.h"
#include "preferences/usersettings.h"

class Library;
class TrackCollection;
class WLibrary;
class KeyboardEventFilter;

/// Sidebar feature that shows the library organized by the directories on disk
/// (read from the DB, so it is fast and reflects imported tracks).
class FoldersFeature : public BaseTrackSetFeature {
    Q_OBJECT

  public:
    FoldersFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~FoldersFeature() override = default;

    QVariant title() override;

    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;

    TreeItemModel* sidebarModel() const override;

  public slots:
    void activateChild(const QModelIndex& index) override;

  private:
    void rebuildChildModel();

    TrackCollection* const m_pTrackCollection;
    FoldersTableModel m_foldersTableModel;
};
