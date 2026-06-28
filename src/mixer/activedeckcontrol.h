#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

#include "preferences/usersettings.h"

class ControlObject;
class PlayerManager;

/// Tracks which deck is currently "active" (the focused deck) and mirrors that
/// state into per-deck `[ChannelN],focus` controls so skins can highlight it.
///
/// The active deck is exposed app-wide as `[App],active_deck` (1-indexed, with
/// a legacy `[Master],active_deck` alias). Exactly one `[ChannelN],focus`
/// control is 1 at any time; the rest are 0.
class ActiveDeckControl : public QObject {
    Q_OBJECT
  public:
    ActiveDeckControl(PlayerManager* pPlayerManager,
            UserSettingsPointer pConfig,
            QObject* parent = nullptr);
    ~ActiveDeckControl() override;

    /// Currently active deck, 1-indexed. 0 if no deck exists yet.
    int activeDeck() const;
    /// Group of the active deck, e.g. "[Channel1]". Empty if no deck exists.
    QString activeDeckGroup() const;

    /// Make deck `deckNumberOneBased` the active deck (clamped to existing decks).
    void setActiveDeck(int deckNumberOneBased);
    /// Make the deck identified by `group` (e.g. "[Channel2]") active.
    /// No-op if `group` is not a deck group.
    void setActiveDeckByGroup(const QString& group);

    /// Called when a track is loaded into `group`. Makes that deck active if
    /// the `[ActiveDeck],follow_track_load` setting is enabled.
    void onTrackLoaded(const QString& group);

  public slots:
    /// Create focus controls for newly added decks and refresh the mirrors.
    void slotNumberOfDecksChanged(int decks);

  private slots:
    /// Handles external requests to set `[App],active_deck` (keyboard/MIDI/etc.).
    void slotActiveDeckChangeRequest(double v);

  private:
    void applyActiveDeck(int deckNumberOneBased);
    void updateFocusMirrors();
    int deckCount() const;

    PlayerManager* m_pPlayerManager;
    UserSettingsPointer m_pConfig;
    std::unique_ptr<ControlObject> m_pCOActiveDeck;
    // [ActiveDeck],follow_track_load setting (1 = loading a track focuses its deck)
    std::unique_ptr<ControlObject> m_pCOFollowTrackLoad;
    // One focus control per deck; index 0 -> [Channel1],focus
    std::vector<std::unique_ptr<ControlObject>> m_focusControls;
};
