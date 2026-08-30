#pragma once

#include <QTimer>
#include <QWidget>
#include <memory>

#include "control/controlproxy.h"
#include "control/controlpushbutton.h"

class QDomNode;
class SkinContext;

/// Floating "mix pad" to beatmatch a deck with the mouse only:
/// a long pitch fader, a nudge strip (temporary bend with spring-back via the
/// jog control), play/cue zones and a hotcue row. One pad per deck, each
/// bound to its own [ChannelN] group and toggled via [ChannelN],mixpad_show
/// (a PAD button in the deck). Floats on top of the skin as a frameless tool
/// window, draggable by its header and minimizable to just the header bar.
class WMixPad : public QWidget {
    Q_OBJECT
  public:
    WMixPad(QWidget* pParent, int deckNumber);
    void setup(const QDomNode& node, const SkinContext& context);

  protected:
    void paintEvent(QPaintEvent* pEvent) override;
    void mousePressEvent(QMouseEvent* pEvent) override;
    void mouseMoveEvent(QMouseEvent* pEvent) override;
    void mouseReleaseEvent(QMouseEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override;
    void wheelEvent(QWheelEvent* pEvent) override;
    void showEvent(QShowEvent* pEvent) override;

  private slots:
    void slotShowChanged(double v);
    void slotDeckUpdate(double v);
    void slotNudgeTick();

  private:
    enum class Zone {
        None,
        Header,
        Minimize,
        Close,
        Pitch,
        Nudge,
        Play,
        Cue,
        StopCue,
        Hotcue,
    };
    Zone zoneAt(const QPoint& pos) const;
    void updateDeckProxies();
    void sendWheelNudge(int angleDeltaY);
    QRect headerRect() const;
    QRect minimizeRect() const;
    QRect closeRect() const;
    QRect pitchRect() const;
    QRect nudgeRect() const;
    QRect playRect() const;
    QRect cueRect() const;
    QRect stopCueRect() const;
    QRect hotcueRect(int index) const;
    int hotcueAt(const QPoint& pos) const;

    // [ChannelN],mixpad_show — bound to the deck's PAD button. The skin
    // parser may have auto-created the control before us; we only own it when
    // nobody else created it, and always talk through the proxy.
    std::unique_ptr<ControlPushButton> m_pShowControl;
    std::unique_ptr<ControlProxy> m_pShowProxy;
    // Proxies for this pad's deck.
    std::unique_ptr<ControlProxy> m_pRate;
    std::unique_ptr<ControlProxy> m_pJog;
    std::unique_ptr<ControlProxy> m_pPlay;
    std::unique_ptr<ControlProxy> m_pCue;
    std::unique_ptr<ControlProxy> m_pCueGotoAndStop;
    std::unique_ptr<ControlProxy> m_pBpm;
    static constexpr int kHotcueCount = 4;
    std::unique_ptr<ControlProxy> m_pHotcueActivate[kHotcueCount];
    std::unique_ptr<ControlProxy> m_pHotcueSet[kHotcueCount];
    std::unique_ptr<ControlProxy> m_pHotcueStatus[kHotcueCount];
    std::unique_ptr<ControlProxy> m_pHotcueClear[kHotcueCount];
    int m_activeHotcue;
    QString m_group;
    int m_deckNumber;

    // While the nudge strip is held, this value is fed into the jog
    // accumulator on every tick; releasing stops the timer and the engine's
    // jog filter decays back to zero (the "spring").
    QTimer m_nudgeTimer;
    double m_nudgeValue;

    Zone m_dragZone;
    QPoint m_dragStartGlobal;
    QPoint m_padStartPos;
    double m_dragStartRate;
    int m_dragStartY;
    bool m_minimized;
    bool m_positioned = false;
};
