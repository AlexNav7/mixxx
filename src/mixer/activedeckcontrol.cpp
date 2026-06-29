#include "mixer/activedeckcontrol.h"

#include <cmath>
#include <utility>

#include "control/controlobject.h"
#include "control/controlpushbutton.h"
#include "controllers/midi/midimessage.h"
#include "mixer/playermanager.h"
#include "util/math.h"

#include "moc_activedeckcontrol.cpp"

namespace {
const QString kAppGroup = QStringLiteral("[App]");
const QString kLegacyGroup = QStringLiteral("[Master]");
const QString kActiveDeckGroup = QStringLiteral("[ActiveDeck]");
// Number of palette colors offered for the active-deck highlight.
constexpr int kNumHighlightColors = 6;
} // namespace

ActiveDeckControl::ActiveDeckControl(PlayerManager* pPlayerManager,
        UserSettingsPointer pConfig,
        QObject* parent)
        : QObject(parent),
          m_pPlayerManager(pPlayerManager),
          m_pConfig(pConfig),
          m_pCOActiveDeck(std::make_unique<ControlObject>(
                  ConfigKey(kAppGroup, QStringLiteral("active_deck")),
                  true,   // bIgnoreNops
                  false,  // bTrack
                  true,   // bPersist
                  1.0)),  // default: deck 1
          m_pCOFollowTrackLoad(std::make_unique<ControlObject>(
                  ConfigKey(kActiveDeckGroup, QStringLiteral("follow_track_load")),
                  true,   // bIgnoreNops
                  false,  // bTrack
                  true,   // bPersist
                  1.0)),  // default: enabled
          m_pCOHighlightColor(std::make_unique<ControlObject>(
                  ConfigKey(kActiveDeckGroup, QStringLiteral("highlight_color")),
                  true,   // bIgnoreNops
                  false,  // bTrack
                  true,   // bPersist
                  1.0)) { // default: first palette color
    m_pCOActiveDeck->addAlias(ConfigKey(kLegacyGroup, QStringLiteral("active_deck")));
    // Validate color changes (ignore the spurious 0 a momentary skin button
    // writes on release) and refresh the colored mirrors when it changes.
    m_pCOHighlightColor->connectValueChangeRequest(this,
            &ActiveDeckControl::slotHighlightColorChangeRequest,
            Qt::DirectConnection);
    connect(m_pCOHighlightColor.get(),
            &ControlObject::valueChanged,
            this,
            [this](double) { updateFocusMirrors(); });
    m_pCOActiveDeck->connectValueChangeRequest(this,
            &ActiveDeckControl::slotActiveDeckChangeRequest,
            Qt::DirectConnection);

    // [ActiveDeck] action controls: each forwards to the active deck's
    // same-named control, preserving press/release (button) semantics. These
    // can be mapped from the keyboard, MIDI controllers or skins.
    const QString actionKeys[] = {
            // transport
            QStringLiteral("play"),
            QStringLiteral("stop"),
            QStringLiteral("cue_default"),
            QStringLiteral("cue_set"),
            QStringLiteral("beatsync"),
            // hotcues: activate + set
            QStringLiteral("hotcue_1_activate"),
            QStringLiteral("hotcue_2_activate"),
            QStringLiteral("hotcue_3_activate"),
            QStringLiteral("hotcue_4_activate"),
            QStringLiteral("hotcue_1_set"),
            QStringLiteral("hotcue_2_set"),
            QStringLiteral("hotcue_3_set"),
            QStringLiteral("hotcue_4_set"),
            // beatjump (jump X beats; size_halve/double change X)
            QStringLiteral("beatjump_backward"),
            QStringLiteral("beatjump_forward"),
            QStringLiteral("beatjump_size_halve"),
            QStringLiteral("beatjump_size_double"),
    };
    for (const QString& key : actionKeys) {
        // ControlPushButton (not a plain ControlObject) so it accepts
        // keyboard/MIDI input via setValueFromMidi.
        auto pControl = std::make_unique<ControlPushButton>(ConfigKey(kActiveDeckGroup, key));
        connect(pControl.get(),
                &ControlObject::valueChanged,
                this,
                [this, key](double v) { forwardToActiveDeck(key, v); });
        m_actionControls.push_back(std::move(pControl));
    }
}

void ActiveDeckControl::forwardToActiveDeck(const QString& key, double value) {
    const QString group = activeDeckGroup();
    if (group.isEmpty()) {
        return;
    }
    ControlObject* pTarget = ControlObject::getControl(ConfigKey(group, key));
    if (pTarget) {
        pTarget->setValueFromMidi(
                value != 0.0 ? MidiOpCode::NoteOn : MidiOpCode::NoteOff, value);
    }
}

ActiveDeckControl::~ActiveDeckControl() = default;

int ActiveDeckControl::deckCount() const {
    return m_pPlayerManager ? m_pPlayerManager->numberOfDecks() : 0;
}

int ActiveDeckControl::activeDeck() const {
    const int decks = deckCount();
    if (decks <= 0) {
        return 0;
    }
    const int n = static_cast<int>(m_pCOActiveDeck->get());
    return math_clamp(n, 1, decks);
}

QString ActiveDeckControl::activeDeckGroup() const {
    const int n = activeDeck();
    if (n <= 0) {
        return QString();
    }
    return PlayerManager::groupForDeck(n - 1);
}

void ActiveDeckControl::setActiveDeck(int deckNumberOneBased) {
    applyActiveDeck(deckNumberOneBased);
}

void ActiveDeckControl::setActiveDeckByGroup(const QString& group) {
    int number = 0;
    if (PlayerManager::isDeckGroup(group, &number)) {
        applyActiveDeck(number);
    }
}

void ActiveDeckControl::slotActiveDeckChangeRequest(double v) {
    applyActiveDeck(static_cast<int>(std::lround(v)));
}

void ActiveDeckControl::slotHighlightColorChangeRequest(double v) {
    const int color = static_cast<int>(std::lround(v));
    if (color >= 1 && color <= kNumHighlightColors) {
        m_pCOHighlightColor->setAndConfirm(color);
    }
    // Ignore out-of-range requests (e.g. the momentary button's release 0).
}

void ActiveDeckControl::onTrackLoaded(const QString& group) {
    if (m_pCOFollowTrackLoad && m_pCOFollowTrackLoad->get() != 0.0) {
        setActiveDeckByGroup(group);
    }
}

void ActiveDeckControl::slotNumberOfDecksChanged(int decks) {
    // Create focus + focus_request controls for any newly added decks.
    while (static_cast<int>(m_focusControls.size()) < decks) {
        const int idx = static_cast<int>(m_focusControls.size());
        const QString group = PlayerManager::groupForDeck(idx);

        // Output mirror: 1 on the active deck, 0 elsewhere (skins bind this).
        m_focusControls.push_back(std::make_unique<ControlObject>(
                ConfigKey(group, QStringLiteral("focus"))));

        // Input request: any trigger sets this to make this deck active.
        auto pRequest = std::make_unique<ControlObject>(
                ConfigKey(group, QStringLiteral("focus_request")));
        const int deckNumber = idx + 1;
        connect(pRequest.get(),
                &ControlObject::valueChanged,
                this,
                [this, deckNumber](double v) {
                    if (v != 0.0) {
                        applyActiveDeck(deckNumber);
                    }
                });
        m_focusRequestControls.push_back(std::move(pRequest));
    }
    // Re-apply to clamp the (possibly persisted) value and refresh mirrors.
    applyActiveDeck(static_cast<int>(m_pCOActiveDeck->get()));
}

void ActiveDeckControl::applyActiveDeck(int deckNumberOneBased) {
    const int decks = deckCount();
    if (decks <= 0) {
        // No decks yet: store the requested value (>= 1) so it persists;
        // mirrors will be set once decks are created.
        m_pCOActiveDeck->setAndConfirm(math_max(1, deckNumberOneBased));
        return;
    }
    const int clamped = math_clamp(deckNumberOneBased, 1, decks);
    m_pCOActiveDeck->setAndConfirm(clamped);
    updateFocusMirrors();
}

void ActiveDeckControl::updateFocusMirrors() {
    const int active = activeDeck(); // clamped, 1-indexed
    const int color = math_clamp(
            static_cast<int>(m_pCOHighlightColor->get()), 1, kNumHighlightColors);
    for (int i = 0; i < static_cast<int>(m_focusControls.size()); ++i) {
        // 0 when inactive; the palette color index (1..N) when this is the
        // active deck, so skins can color it via QSS.
        m_focusControls[i]->set((i + 1) == active ? static_cast<double>(color) : 0.0);
    }
}
