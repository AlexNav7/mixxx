#pragma once

#include <QPushButton>

#include "control/pollingcontrolproxy.h"
#include "library/tabledelegates/tableitemdelegate.h"
#include "track/track_decl.h"
#include "util/parented_ptr.h"
#ifdef __STEM__
#include "engine/engine.h"
#endif

class ControlProxy;
class WLibraryTableView;

// A QPushButton for rendering the library preview button within the
// PreviewButtonDelegate.
class LibraryPreviewButton : public QPushButton {
    Q_OBJECT
  public:
    explicit LibraryPreviewButton(QWidget* parent);

    void paint(QPainter* painter);
};

class PreviewButtonDelegate : public TableItemDelegate {
    Q_OBJECT

  public:
    PreviewButtonDelegate(WLibraryTableView* parent, int column);
    ~PreviewButtonDelegate() override;

    QWidget* createEditor(
            QWidget* parent,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;

    // Apparently this no-op override is required to trigger a paint
    // event after row has been painted with the 'selected' style. (Qt 5)
    void setEditorData(
            QWidget* editor,
            const QModelIndex& index) const override;
    // Seems this is not required
    void setModelData(
            QWidget* editor,
            QAbstractItemModel* model,
            const QModelIndex& index) const override;
    void paintItem(
            QPainter* painter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;
    QSize sizeHint(
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;

  signals:
#ifdef __STEM__
    void loadTrackToPlayer(const TrackPointer& pTrack,
            const QString& group,
            mixxx::StemChannelSelection stemMask,
            bool);
#else
    void loadTrackToPlayer(const TrackPointer& pTrack, const QString& group, bool);
#endif
    void buttonSetChecked(bool);

  public slots:
    void cellEntered(const QModelIndex& index);
    void buttonClicked();
    void previewDeckPlayChanged(double v);

  private:
    bool isPreviewDeckPlaying() const;
    bool isTrackLoadedInPreviewDeck(
            const QModelIndex& index) const;
    bool isTrackLoadedInPreviewDeckAndPlaying(
            const QModelIndex& index) const {
        if (!isPreviewDeckPlaying()) {
            // No need to query additional data from the table
            return false;
        }
        return isTrackLoadedInPreviewDeck(index);
    }

    const int m_column;

    const parented_ptr<ControlProxy> m_pPreviewDeckPlay;
    // preview-active-deck: [App],active_deck — route preview to the active deck
    const parented_ptr<ControlProxy> m_pActiveDeck;
    PollingControlProxy m_pCueGotoAndPlay;

    const parented_ptr<LibraryPreviewButton> m_pButton;

    QPersistentModelIndex m_currentEditedCellIndex;

    // preview-active-deck: group of the deck currently holding a transient
    // preview loaded by us (empty when none). Used to eject/replace it.
    QString m_activePreviewGroup;
};
