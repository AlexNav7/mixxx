#include "mixer/activedeckcontrol.h"

#include <cmath>
#include <utility>

#include "control/controlobject.h"
#include "mixer/playermanager.h"
#include "util/math.h"

#include "moc_activedeckcontrol.cpp"

namespace {
const QString kAppGroup = QStringLiteral("[App]");
const QString kLegacyGroup = QStringLiteral("[Master]");
const QString kActiveDeckGroup = QStringLiteral("[ActiveDeck]");
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
                  1.0)) { // default: enabled
    m_pCOActiveDeck->addAlias(ConfigKey(kLegacyGroup, QStringLiteral("active_deck")));
    m_pCOActiveDeck->connectValueChangeRequest(this,
            &ActiveDeckControl::slotActiveDeckChangeRequest,
            Qt::DirectConnection);
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
    for (int i = 0; i < static_cast<int>(m_focusControls.size()); ++i) {
        m_focusControls[i]->set((i + 1) == active ? 1.0 : 0.0);
    }
}
