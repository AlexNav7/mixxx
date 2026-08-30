#include "widget/wmixpad.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "control/controlobject.h"
#include "moc_wmixpad.cpp"
#include "util/math.h"

namespace {

constexpr int kPadWidth = 184;
constexpr int kPadHeight = 374;
constexpr int kHeaderHeight = 24;
constexpr int kMargin = 8;
constexpr int kHotcueHeight = 28;
// Bottom of the main zones (strips/buttons); below sits the hotcue row.
constexpr int kBodyBottom = kPadHeight - kMargin - kHotcueHeight - 6;

// Nudge strength: jog value per pixel of drag displacement (the engine
// multiplies the filtered jog by 0.1 to obtain the rate offset).
constexpr double kNudgePerPixel = 0.006;
constexpr double kNudgeMax = 2.0;
// One wheel notch = one small push that decays by itself.
constexpr double kWheelNudge = 0.8;
constexpr int kNudgeTickMs = 20;

const QColor kBgColor(24, 24, 26, 242);
const QColor kZoneColor(42, 42, 46);
const QColor kBorderColor(90, 90, 96);
const QColor kTextColor(200, 200, 204);
const QColor kPitchAccent(255, 159, 67);
const QColor kNudgeAccent(84, 160, 255);
const QColor kPlayAccent(46, 204, 113);
const QColor kCueAccent(255, 118, 117);

} // anonymous namespace

WMixPad::WMixPad(QWidget* pParent, int deckNumber)
        // Frameless tool window: a real (native) window, so it stays on top of
        // the OpenGL waveform viewports too — plain child widgets can't
        // (native GL windows always paint over sibling widgets).
        : QWidget(pParent, Qt::Tool | Qt::FramelessWindowHint),
          m_activeHotcue(-1),
          m_deckNumber(deckNumber),
          m_nudgeValue(0.0),
          m_dragZone(Zone::None),
          m_dragStartRate(0.0),
          m_dragStartY(0),
          m_minimized(false) {
    setObjectName(QStringLiteral("MixPad"));
    setFixedSize(kPadWidth, kPadHeight);
    m_group = QStringLiteral("[Channel%1]").arg(m_deckNumber);

    const ConfigKey showKey(m_group, QStringLiteral("mixpad_show"));
    // The deck's PAD button may have auto-created this control already (skin
    // nodes are parsed before us). Only create it if it doesn't exist, and
    // always listen through a proxy so we follow whichever instance won.
    if (!ControlObject::getControl(showKey, ControlFlag::NoAssertIfMissing)) {
        m_pShowControl = std::make_unique<ControlPushButton>(showKey);
        m_pShowControl->setButtonMode(mixxx::control::ButtonMode::Toggle);
    }
    m_pShowProxy = std::make_unique<ControlProxy>(showKey);
    m_pShowProxy->connectValueChanged(this, &WMixPad::slotShowChanged);

    m_nudgeTimer.setInterval(kNudgeTickMs);
    connect(&m_nudgeTimer, &QTimer::timeout, this, &WMixPad::slotNudgeTick);

    updateDeckProxies();
    setVisible(m_pShowProxy->toBool());
}

void WMixPad::setup(const QDomNode& node, const SkinContext& context) {
    Q_UNUSED(node);
    Q_UNUSED(context);
}

void WMixPad::updateDeckProxies() {
    const QString& group = m_group;
    m_pRate = std::make_unique<ControlProxy>(group, QStringLiteral("rate"));
    m_pRate->connectValueChanged(this, &WMixPad::slotDeckUpdate);
    m_pJog = std::make_unique<ControlProxy>(group, QStringLiteral("jog"));
    m_pPlay = std::make_unique<ControlProxy>(group, QStringLiteral("play"));
    m_pPlay->connectValueChanged(this, &WMixPad::slotDeckUpdate);
    m_pCue = std::make_unique<ControlProxy>(group, QStringLiteral("cue_default"));
    m_pCueGotoAndStop = std::make_unique<ControlProxy>(
            group, QStringLiteral("cue_gotoandstop"));
    m_pBpm = std::make_unique<ControlProxy>(group, QStringLiteral("bpm"));
    m_pBpm->connectValueChanged(this, &WMixPad::slotDeckUpdate);
    for (int i = 0; i < kHotcueCount; ++i) {
        m_pHotcueActivate[i] = std::make_unique<ControlProxy>(group,
                QStringLiteral("hotcue_%1_activate").arg(i + 1));
        m_pHotcueSet[i] = std::make_unique<ControlProxy>(group,
                QStringLiteral("hotcue_%1_set").arg(i + 1));
        m_pHotcueStatus[i] = std::make_unique<ControlProxy>(group,
                QStringLiteral("hotcue_%1_status").arg(i + 1));
        m_pHotcueStatus[i]->connectValueChanged(this, &WMixPad::slotDeckUpdate);
        m_pHotcueClear[i] = std::make_unique<ControlProxy>(group,
                QStringLiteral("hotcue_%1_clear").arg(i + 1));
    }
    update();
}

void WMixPad::slotShowChanged(double v) {
    setVisible(v > 0.0);
}

void WMixPad::slotDeckUpdate(double v) {
    Q_UNUSED(v);
    update();
}

void WMixPad::slotNudgeTick() {
    if (m_pJog && m_nudgeValue != 0.0) {
        // The jog control is an accumulator: add, don't overwrite.
        m_pJog->set(m_pJog->get() + m_nudgeValue);
    }
}

void WMixPad::sendWheelNudge(int angleDeltaY) {
    if (!m_pJog || angleDeltaY == 0) {
        return;
    }
    const double impulse = (angleDeltaY > 0) ? kWheelNudge : -kWheelNudge;
    m_pJog->set(m_pJog->get() + impulse);
}

void WMixPad::showEvent(QShowEvent* pEvent) {
    QWidget::showEvent(pEvent);
    // First show: place it over the parent window (global coordinates —
    // the pad is a top-level tool window).
    if (!m_positioned && parentWidget()) {
        // Stagger per deck so several open pads don't overlap exactly.
        const QRect parentGeom = parentWidget()->window()->geometry();
        move(parentGeom.left() + 280 + (m_deckNumber - 1) * (kPadWidth + 16),
                parentGeom.top() + 140);
        m_positioned = true;
    }
    raise();
}

QRect WMixPad::headerRect() const {
    return QRect(0, 0, width(), kHeaderHeight);
}

QRect WMixPad::minimizeRect() const {
    return QRect(width() - 40, 3, 16, kHeaderHeight - 6);
}

QRect WMixPad::closeRect() const {
    return QRect(width() - 20, 3, 16, kHeaderHeight - 6);
}

QRect WMixPad::nudgeRect() const {
    // Nudge strip first (left), pitch next — order requested by the user.
    return QRect(kMargin,
            kHeaderHeight + kMargin,
            56,
            kBodyBottom - kHeaderHeight - kMargin);
}

QRect WMixPad::pitchRect() const {
    return QRect(kMargin + 56 + 6,
            kHeaderHeight + kMargin,
            56,
            kBodyBottom - kHeaderHeight - kMargin);
}

QRect WMixPad::playRect() const {
    const int x = kMargin + 56 + 6 + 56 + 6;
    const int top = kHeaderHeight + kMargin;
    const int totalH = kBodyBottom - kHeaderHeight - kMargin;
    return QRect(x, top, width() - x - kMargin, (totalH - 12) / 3);
}

// Button order (top to bottom): PLAY, stop-and-back-to-cue, CUE.
QRect WMixPad::stopCueRect() const {
    const int x = kMargin + 56 + 6 + 56 + 6;
    const int top = kHeaderHeight + kMargin;
    const int totalH = kBodyBottom - kHeaderHeight - kMargin;
    const int third = (totalH - 12) / 3;
    return QRect(x, top + third + 6, width() - x - kMargin, third);
}

QRect WMixPad::cueRect() const {
    const int x = kMargin + 56 + 6 + 56 + 6;
    const int top = kHeaderHeight + kMargin;
    const int totalH = kBodyBottom - kHeaderHeight - kMargin;
    const int third = (totalH - 12) / 3;
    return QRect(x, top + 2 * (third + 6), width() - x - kMargin, totalH - 2 * (third + 6));
}

QRect WMixPad::hotcueRect(int index) const {
    const int w = (width() - 2 * kMargin - 3 * 6) / kHotcueCount;
    return QRect(kMargin + index * (w + 6), kBodyBottom + 6, w, kHotcueHeight);
}

int WMixPad::hotcueAt(const QPoint& pos) const {
    for (int i = 0; i < kHotcueCount; ++i) {
        if (hotcueRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

WMixPad::Zone WMixPad::zoneAt(const QPoint& pos) const {
    if (closeRect().contains(pos)) {
        return Zone::Close;
    }
    if (minimizeRect().contains(pos)) {
        return Zone::Minimize;
    }
    if (headerRect().contains(pos)) {
        return Zone::Header;
    }
    if (m_minimized) {
        return Zone::None;
    }
    if (pitchRect().contains(pos)) {
        return Zone::Pitch;
    }
    if (nudgeRect().contains(pos)) {
        return Zone::Nudge;
    }
    if (playRect().contains(pos)) {
        return Zone::Play;
    }
    if (cueRect().contains(pos)) {
        return Zone::Cue;
    }
    if (stopCueRect().contains(pos)) {
        return Zone::StopCue;
    }
    if (hotcueAt(pos) >= 0) {
        return Zone::Hotcue;
    }
    return Zone::None;
}

void WMixPad::mousePressEvent(QMouseEvent* pEvent) {
    if (pEvent->button() == Qt::RightButton) {
        // Right click on a hotcue clears it.
        const int hotcue = hotcueAt(pEvent->pos());
        if (hotcue >= 0 && m_pHotcueClear[hotcue]) {
            m_pHotcueClear[hotcue]->set(1.0);
            m_pHotcueClear[hotcue]->set(0.0);
        }
        return;
    }
    if (pEvent->button() != Qt::LeftButton) {
        return;
    }
    const Zone zone = zoneAt(pEvent->pos());
    switch (zone) {
    case Zone::Close:
        // A ControlProxy doesn't get notified of its own set() — hide directly
        // and update the control so the deck's PAD button reflects the state.
        m_pShowProxy->set(0.0);
        setVisible(false);
        return;
    case Zone::Minimize:
        m_minimized = !m_minimized;
        setFixedSize(kPadWidth, m_minimized ? kHeaderHeight : kPadHeight);
        update();
        return;
    case Zone::Header:
        m_dragZone = Zone::Header;
        m_dragStartGlobal = pEvent->globalPosition().toPoint();
        m_padStartPos = pos();
        return;
    case Zone::Pitch:
        m_dragZone = Zone::Pitch;
        m_dragStartY = pEvent->pos().y();
        m_dragStartRate = m_pRate ? m_pRate->get() : 0.0;
        return;
    case Zone::Nudge:
        m_dragZone = Zone::Nudge;
        m_dragStartY = pEvent->pos().y();
        m_nudgeValue = 0.0;
        m_nudgeTimer.start();
        update();
        return;
    case Zone::Play:
        if (m_pPlay) {
            m_pPlay->set(m_pPlay->toBool() ? 0.0 : 1.0);
        }
        return;
    case Zone::Cue:
        m_dragZone = Zone::Cue;
        if (m_pCue) {
            m_pCue->set(1.0);
        }
        update();
        return;
    case Zone::StopCue:
        // Stop and jump back to the cue point.
        if (m_pCueGotoAndStop) {
            m_pCueGotoAndStop->set(1.0);
            m_pCueGotoAndStop->set(0.0);
        }
        return;
    case Zone::Hotcue: {
        const int hotcue = hotcueAt(pEvent->pos());
        if (hotcue < 0) {
            return;
        }
        const bool isSet = m_pHotcueStatus[hotcue] && m_pHotcueStatus[hotcue]->get() > 0;
        if (!isSet) {
            // Empty: record silently at the current position (no preview quirk).
            if (m_pHotcueSet[hotcue]) {
                m_pHotcueSet[hotcue]->set(1.0);
                m_pHotcueSet[hotcue]->set(0.0);
            }
        } else if (m_pHotcueActivate[hotcue]) {
            // Set: jump while playing / preview while held when stopped.
            m_dragZone = Zone::Hotcue;
            m_activeHotcue = hotcue;
            m_pHotcueActivate[hotcue]->set(1.0);
        }
        update();
        return;
    }
    default:
        // Dragging any empty area moves the pad, same as the header.
        m_dragZone = Zone::Header;
        m_dragStartGlobal = pEvent->globalPosition().toPoint();
        m_padStartPos = pos();
        return;
    }
}

void WMixPad::mouseMoveEvent(QMouseEvent* pEvent) {
    switch (m_dragZone) {
    case Zone::Header: {
        const QPoint delta = pEvent->globalPosition().toPoint() - m_dragStartGlobal;
        QPoint newPos = m_padStartPos + delta;
        // Global coordinates (top-level window): keep it inside the Mixxx window.
        if (parentWidget()) {
            const QRect bounds = parentWidget()->window()->geometry();
            newPos.setX(math_clamp(newPos.x(), bounds.left(), bounds.right() - width()));
            newPos.setY(math_clamp(newPos.y(), bounds.top(), bounds.bottom() - kHeaderHeight));
        }
        move(newPos);
        return;
    }
    case Zone::Pitch: {
        if (!m_pRate) {
            return;
        }
        // Full strip height = full rate range; upwards = faster.
        const double perPixel = 2.0 / pitchRect().height();
        const double delta = (m_dragStartY - pEvent->pos().y()) * perPixel;
        m_pRate->set(math_clamp(m_dragStartRate + delta, -1.0, 1.0));
        update();
        return;
    }
    case Zone::Nudge: {
        // Displacement from the grab point = push/brake strength (spring on release).
        const double displacement = m_dragStartY - pEvent->pos().y();
        m_nudgeValue = math_clamp(
                displacement * kNudgePerPixel, -kNudgeMax, kNudgeMax);
        update();
        return;
    }
    default:
        return;
    }
}

void WMixPad::mouseReleaseEvent(QMouseEvent* pEvent) {
    Q_UNUSED(pEvent);
    if (m_dragZone == Zone::Nudge) {
        m_nudgeTimer.stop();
        m_nudgeValue = 0.0;
    } else if (m_dragZone == Zone::Cue) {
        if (m_pCue) {
            m_pCue->set(0.0);
        }
    } else if (m_dragZone == Zone::Hotcue) {
        if (m_activeHotcue >= 0 && m_pHotcueActivate[m_activeHotcue]) {
            m_pHotcueActivate[m_activeHotcue]->set(0.0);
        }
        m_activeHotcue = -1;
    }
    m_dragZone = Zone::None;
    update();
}

void WMixPad::mouseDoubleClickEvent(QMouseEvent* pEvent) {
    // Double click on the pitch strip resets the pitch to 0%.
    if (zoneAt(pEvent->pos()) == Zone::Pitch && m_pRate) {
        m_pRate->set(0.0);
        update();
    }
}

void WMixPad::wheelEvent(QWheelEvent* pEvent) {
    // The wheel nudges anywhere on the pad — including while the pitch fader
    // is grabbed (index finger drags, middle finger pushes: the "two hands on
    // the turntable" phase).
    sendWheelNudge(pEvent->angleDelta().y());
    pEvent->accept();
}

void WMixPad::paintEvent(QPaintEvent* pEvent) {
    Q_UNUSED(pEvent);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Frame
    p.setPen(QPen(kBorderColor, 1));
    p.setBrush(kBgColor);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);

    // Header: clearly visible grab bar — grip dots + deck + BPM + buttons.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(64, 64, 72));
    p.drawRoundedRect(QRect(1, 1, width() - 2, kHeaderHeight - 1), 4, 4);
    QFont font = p.font();
    font.setPixelSize(11);
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor(150, 150, 158));
    p.drawText(QRect(6, 0, 18, kHeaderHeight), Qt::AlignCenter, QStringLiteral("⣿"));
    p.setPen(QColor(235, 235, 240));
    const QString bpmText = (m_pBpm && m_pBpm->get() > 0)
            ? QStringLiteral(" · %1").arg(QString::number(m_pBpm->get(), 'f', 1))
            : QString();
    p.drawText(headerRect().adjusted(26, 0, -44, 0),
            Qt::AlignVCenter | Qt::AlignLeft,
            QStringLiteral("DECK %1%2").arg(m_deckNumber).arg(bpmText));
    p.drawText(minimizeRect(), Qt::AlignCenter, m_minimized ? QStringLiteral("+") : QStringLiteral("–"));
    p.drawText(closeRect(), Qt::AlignCenter, QStringLiteral("×"));

    if (m_minimized) {
        return;
    }

    font.setPixelSize(10);
    p.setFont(font);

    // Pitch strip
    const QRect pitch = pitchRect();
    p.setPen(QPen(kBorderColor, 1));
    p.setBrush(kZoneColor);
    p.drawRoundedRect(pitch, 3, 3);
    // center line
    p.setPen(QPen(kBorderColor, 1));
    p.drawLine(pitch.left() + 4, pitch.center().y(), pitch.right() - 4, pitch.center().y());
    // handle
    const double rate = m_pRate ? m_pRate->get() : 0.0;
    const int handleY = pitch.center().y() - static_cast<int>(rate * (pitch.height() / 2.0 - 6));
    p.setPen(Qt::NoPen);
    p.setBrush(kPitchAccent);
    p.drawRoundedRect(QRect(pitch.left() + 3, handleY - 4, pitch.width() - 6, 8), 2, 2);
    p.setPen(kTextColor);
    p.drawText(pitch.adjusted(0, 4, 0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("PITCH"));

    // Nudge strip
    const QRect nudge = nudgeRect();
    p.setPen(QPen(kBorderColor, 1));
    p.setBrush(kZoneColor);
    p.drawRoundedRect(nudge, 3, 3);
    p.setPen(QPen(kBorderColor, 1));
    p.drawLine(nudge.left() + 3, nudge.center().y(), nudge.right() - 3, nudge.center().y());
    // displacement indicator while nudging
    if (m_dragZone == Zone::Nudge && m_nudgeValue != 0.0) {
        const int offset = static_cast<int>(
                (m_nudgeValue / kNudgeMax) * (nudge.height() / 2.0 - 8));
        p.setPen(Qt::NoPen);
        p.setBrush(kNudgeAccent);
        p.drawRoundedRect(QRect(nudge.left() + 3,
                                  nudge.center().y() - offset - 3,
                                  nudge.width() - 6,
                                  6),
                2,
                2);
    }
    p.setPen(m_dragZone == Zone::Nudge ? kNudgeAccent : kTextColor);
    p.save();
    p.translate(nudge.center().x() + 4, nudge.center().y() + 24);
    p.rotate(-90);
    p.drawText(QRect(-40, -8, 80, 16), Qt::AlignCenter, QStringLiteral("MENEO"));
    p.restore();

    // Play zone
    const QRect play = playRect();
    const bool playing = m_pPlay && m_pPlay->toBool();
    p.setPen(QPen(playing ? kPlayAccent : kBorderColor, 1));
    p.setBrush(playing ? QColor(46, 204, 113, 60) : kZoneColor);
    p.drawRoundedRect(play, 3, 3);
    p.setPen(playing ? kPlayAccent : kTextColor);
    p.drawText(play, Qt::AlignCenter, playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));

    // Stop & back-to-cue zone (above the CUE, order requested by the user)
    const QRect stopCue = stopCueRect();
    p.setPen(QPen(kBorderColor, 1));
    p.setBrush(kZoneColor);
    p.drawRoundedRect(stopCue, 3, 3);
    p.setPen(kTextColor);
    p.drawText(stopCue, Qt::AlignCenter, QStringLiteral("■ CUE"));

    // Cue zone (hold to preview / set while stopped)
    const QRect cue = cueRect();
    const bool cueing = (m_dragZone == Zone::Cue);
    p.setPen(QPen(cueing ? kCueAccent : kBorderColor, 1));
    p.setBrush(cueing ? QColor(255, 118, 117, 60) : kZoneColor);
    p.drawRoundedRect(cue, 3, 3);
    p.setPen(cueing ? kCueAccent : kTextColor);
    p.drawText(cue, Qt::AlignCenter, QStringLiteral("CUE"));

    // Hotcue row 1-4: filled when set; right click clears.
    for (int i = 0; i < kHotcueCount; ++i) {
        const QRect hc = hotcueRect(i);
        const bool set = m_pHotcueStatus[i] && m_pHotcueStatus[i]->get() > 0;
        const bool pressed = (m_dragZone == Zone::Hotcue && m_activeHotcue == i);
        p.setPen(QPen(set ? kPitchAccent : kBorderColor, 1));
        p.setBrush(pressed ? QColor(255, 159, 67, 90)
                           : (set ? QColor(255, 159, 67, 40) : kZoneColor));
        p.drawRoundedRect(hc, 3, 3);
        p.setPen(set ? kPitchAccent : kTextColor);
        p.drawText(hc, Qt::AlignCenter, QString::number(i + 1));
    }
}
