// This file is a part of "Candle" application.
// Copyright 2015-2016 Hayrullin Denis Ravilevich

#include "grblserial.h"
#include "frmsettings.h"
#include <QDebug>

static const QStringList STATUS_STRINGS = {
    "Unknown", "Idle", "Alarm", "Run", "Home",
    "Hold:0", "Hold:1", "Queue", "Check", "Door", "Jog"
};

GrblSerial::GrblSerial(frmSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_serialPort.setParity(QSerialPort::NoParity);
    m_serialPort.setDataBits(QSerialPort::Data8);
    m_serialPort.setFlowControl(QSerialPort::NoFlowControl);
    m_serialPort.setStopBits(QSerialPort::OneStop);

    connect(&m_serialPort, &QSerialPort::readyRead,
            this, &GrblSerial::onReadyRead);
    connect(&m_serialPort,
            static_cast<void(QSerialPort::*)(QSerialPort::SerialPortError)>(&QSerialPort::error),
            this, &GrblSerial::onSerialError);

    connect(&m_timerConnection, &QTimer::timeout, this, &GrblSerial::onTimerConnection);
    connect(&m_timerStateQuery, &QTimer::timeout, this, &GrblSerial::onTimerStateQuery);
}

// ---------------------------------------------------------------------------
// Port management
// ---------------------------------------------------------------------------

void GrblSerial::setPortName(const QString &name) { m_serialPort.setPortName(name); }
void GrblSerial::setBaudRate(qint32 baud)          { m_serialPort.setBaudRate(baud); }
QString GrblSerial::portName() const               { return m_serialPort.portName(); }
qint32  GrblSerial::baudRate() const               { return m_serialPort.baudRate(); }
bool    GrblSerial::isOpen()   const               { return m_serialPort.isOpen(); }

void GrblSerial::openPort()
{
    if (m_serialPort.open(QIODevice::ReadWrite)) {
        m_timerStateQuery.setInterval(m_settings->queryStateTime());
        m_timerStateQuery.start();
        emit portOpened();
    }
}

void GrblSerial::closePort()
{
    m_timerStateQuery.stop();
    if (m_serialPort.isOpen()) m_serialPort.close();
    emit portClosed();
}

// ---------------------------------------------------------------------------
// Timers — started/stopped by frmMain as needed
// ---------------------------------------------------------------------------

void GrblSerial::onTimerConnection()
{
    if (!m_serialPort.isOpen()) {
        openPort();
    } else if (!m_homing && !m_paused && m_queue.isEmpty()) {
        if (m_updateSpindleSpeed) {
            m_updateSpindleSpeed = false;
            enqueue(QString("S%1").arg(m_spindleSpeed), -2,
                    m_settings->showUICommands() ? -2 : -1,  // sentinel: let frmMain handle console
                    m_settings->showUICommands());
        }
        if (m_updateParserStatus) {
            m_updateParserStatus = false;
            enqueue("$G", -3, -1, false);
        }
    }
}

void GrblSerial::onTimerStateQuery()
{
    if (m_serialPort.isOpen() && m_resetCompleted && m_statusReceived) {
        m_serialPort.write(QByteArray(1, '?'));
        m_statusReceived = false;
    }
}

// ---------------------------------------------------------------------------
// Command sending
// ---------------------------------------------------------------------------

void GrblSerial::enqueue(const QString &command, int tableIndex,
                          int consoleIndex, bool showInConsoleIfQueued)
{
    if (!m_serialPort.isOpen() || !m_resetCompleted) return;

    if ((bufferLength() + command.length() + 1) > BUFFERLENGTH) {
        CommandQueue cq;
        cq.command        = command;
        cq.tableIndex     = tableIndex;
        cq.showInConsole  = showInConsoleIfQueued;
        m_queue.append(cq);
        return;
    }

    CommandAttributes ca;
    ca.command      = command;
    ca.length       = command.length() + 1;
    ca.tableIndex   = tableIndex;
    ca.consoleIndex = consoleIndex;
    m_commands.append(ca);
    m_serialPort.write((command + "\r").toLatin1());
}

void GrblSerial::writeRaw(const QByteArray &data)
{
    m_serialPort.write(data);
}

void GrblSerial::reset(int consoleIndex)
{
    qDebug() << "grbl reset";
    m_serialPort.write(QByteArray(1, (char)24));

    m_reseting       = true;
    m_homing         = false;
    m_resetCompleted = false;
    m_statusReceived = true;
    m_lastGrblStatus = -1;
    m_updateSpindleSpeed = true;  // re-sync spindle after reset
    m_updateParserStatus = false;

    m_commands.clear();
    m_queue.clear();

    CommandAttributes ca;
    ca.command      = "[CTRL+X]";
    ca.consoleIndex = consoleIndex;
    ca.tableIndex   = -1;
    ca.length       = ca.command.length() + 1;
    m_commands.append(ca);
}

void GrblSerial::requestSpindleUpdate(int speed)
{
    m_spindleSpeed       = speed;
    m_updateSpindleSpeed = true;
}

void GrblSerial::requestParserUpdate()
{
    m_updateParserStatus = true;
}

void GrblSerial::stopTimers()
{
    m_timerStateQuery.stop();
    m_timerConnection.stop();
}

void GrblSerial::startTimers()
{
    m_timerStateQuery.setInterval(m_settings->queryStateTime());
    m_timerConnection.start(1000);
    m_timerStateQuery.start();
}

bool GrblSerial::hasEndProgramPending() const
{
    return !m_commands.isEmpty() && m_commands.last().command.contains(QRegExp("M0*2|M30"));
}

void GrblSerial::shiftConsoleIndices(int after, int by)
{
    for (int i = 0; i < m_commands.count(); i++) {
        if (m_commands[i].consoleIndex > after) m_commands[i].consoleIndex += by;
    }
}

// ---------------------------------------------------------------------------
// Buffer / queue helpers
// ---------------------------------------------------------------------------

int GrblSerial::bufferLength() const
{
    int length = 0;
    foreach (const CommandAttributes &ca, m_commands) length += ca.length;
    return length;
}

int GrblSerial::queueLength() const
{
    return m_queue.length();
}

bool GrblSerial::fitsInBuffer(const QString &command) const
{
    return (bufferLength() + command.length() + 1) <= BUFFERLENGTH;
}

// ---------------------------------------------------------------------------
// Response classification
// ---------------------------------------------------------------------------

bool GrblSerial::dataIsEnd(const QString &data)
{
    return data.contains("ok") || data.contains("error");
}

bool GrblSerial::dataIsFloating(const QString &data)
{
    static const QStringList floaters = {
        "Reset to continue", "'$H'|'$X' to unlock",
        "ALARM: Soft limit", "ALARM: Hard limit", "Check Door"
    };
    foreach (const QString &s, floaters) {
        if (data.contains(s)) return true;
    }
    return false;
}

bool GrblSerial::dataIsReset(const QString &data)
{
    return QRegExp("^GRBL|GCARVIN\\s\\d\\.\\d.").indexIn(data.toUpper()) != -1;
}

// ---------------------------------------------------------------------------
// Drain overflow queue into serial buffer
// ---------------------------------------------------------------------------

void GrblSerial::drainQueue()
{
    while (!m_queue.isEmpty()) {
        const CommandQueue &front = m_queue.first();
        if (!fitsInBuffer(front.command)) break;

        CommandQueue cq = m_queue.takeFirst();
        sendQueued(cq);
    }
}

void GrblSerial::sendQueued(const CommandQueue &cq)
{
    int consoleIndex = -1;
    if (cq.showInConsole && m_consoleAppender)
        consoleIndex = m_consoleAppender(cq.command);

    CommandAttributes ca;
    ca.command      = cq.command;
    ca.length       = cq.command.length() + 1;
    ca.tableIndex   = cq.tableIndex;
    ca.consoleIndex = consoleIndex;
    m_commands.append(ca);
    m_serialPort.write((cq.command + "\r").toLatin1());
}

// ---------------------------------------------------------------------------
// Main response parser
// ---------------------------------------------------------------------------

void GrblSerial::onReadyRead()
{
    while (m_serialPort.canReadLine()) {
        QString data = m_serialPort.readLine().trimmed();

        // Pre-reset filter: swallow everything until grbl's startup banner
        if (m_reseting) {
            qDebug() << "reseting filter:" << data;
            if (!dataIsReset(data)) continue;
            m_reseting = false;
            m_timerStateQuery.setInterval(m_settings->queryStateTime());
        }

        // ----------------------------------------------------------------
        // Status response  (<Idle|MPos:0,0,0|...>)
        // ----------------------------------------------------------------
        if (!data.isEmpty() && data[0] == '<') {
            m_statusReceived = true;
            GrblStatus st;

            static QRegExp mpx("MPos:([^,]*),([^,]*),([^,^>^|]*)");
            if (mpx.indexIn(data) != -1) {
                st.mposX = mpx.cap(1);
                st.mposY = mpx.cap(2);
                st.mposZ = mpx.cap(3);
            }

            static QRegExp stx("<([^,^>^|]*)");
            if (stx.indexIn(data) != -1) {
                int idx = STATUS_STRINGS.indexOf(stx.cap(1));
                st.machineStatus = (idx == -1) ? GRBL_UNKNOWN : idx;
                if (st.machineStatus != m_lastGrblStatus) m_lastGrblStatus = st.machineStatus;
            }

            static QRegExp wpx("WCO:([^,]*),([^,]*),([^,^>^|]*)");
            if (wpx.indexIn(data) != -1) {
                st.wcoX    = wpx.cap(1).toDouble();
                st.wcoY    = wpx.cap(2).toDouble();
                st.wcoZ    = wpx.cap(3).toDouble();
                st.hasWco  = true;
            }

            static QRegExp ov("Ov:([^,]*),([^,]*),([^,^>^|]*)");
            if (ov.indexIn(data) != -1) {
                st.feedOverride    = ov.cap(1).toInt();
                st.rapidOverride   = ov.cap(2).toInt();
                st.spindleOverride = ov.cap(3).toInt();
                st.hasOverrides    = true;
            }

            static QRegExp pn("Pn:([^|^>]*)");
            if (pn.indexIn(data) != -1) {
                st.pinState    = pn.cap(1);
                st.hasPinState = true;
            }

            static QRegExp as("A:([^,^>^|]+)");
            if (as.indexIn(data) != -1) {
                st.accessoriesState  = as.cap(1);
                st.hasAccessories    = true;
            }

            static QRegExp fs("FS:([^,]*),([^,^|^>]*)");
            if (fs.indexIn(data) != -1) {
                st.currentFeed    = fs.cap(1).toDouble();
                st.currentSpindle = fs.cap(2).toDouble();
                st.hasFeedSpeed   = true;
            }

            emit statusReceived(st);

        // ----------------------------------------------------------------
        // Non-empty line: command response or floating
        // ----------------------------------------------------------------
        } else if (!data.isEmpty()) {

            if (!m_commands.isEmpty() && !dataIsFloating(data)
                    && !(m_commands[0].command != "[CTRL+X]" && dataIsReset(data))) {

                static QString response;

                if ((m_commands[0].command != "[CTRL+X]" && dataIsEnd(data))
                        || (m_commands[0].command == "[CTRL+X]" && dataIsReset(data))) {

                    response.append(data);

                    CommandAttributes ca = m_commands.takeFirst();

                    // Protocol-level state updates
                    if (ca.command.toUpper() == "$G" && ca.tableIndex == -3)
                        m_updateParserStatus = true;  // keep parser status fresh

                    if ((ca.command.toUpper() == "$H" || ca.command.toUpper() == "$T") && m_homing)
                        m_homing = false;

                    if (ca.command == "[CTRL+X]") {
                        m_resetCompleted     = true;
                        m_updateParserStatus = true;
                    }

                    if ((ca.command.contains("M2") || ca.command.contains("M30"))
                            && response.contains("ok") && !response.contains("[Pgm End]")) {
                        m_commands.clear();
                        m_queue.clear();
                    }

                    // Drain overflow queue before notifying frmMain
                    drainQueue();

                    emit commandAcknowledged(ca, response);
                    response.clear();

                } else {
                    response.append(data + "; ");
                }

            } else {
                // Floating or hardware reset
                qDebug() << "floating response:" << data;

                if (dataIsReset(data)) {
                    qDebug() << "hardware reset";
                    m_reseting       = false;
                    m_homing         = false;
                    m_lastGrblStatus = -1;
                    m_commands.clear();
                    m_queue.clear();
                    emit hardwareResetReceived();
                }
                emit floatingResponseReceived(data);
            }
        }
    }
}

void GrblSerial::onSerialError(QSerialPort::SerialPortError error)
{
    static QSerialPort::SerialPortError previousError;
    if (error != QSerialPort::NoError && error != previousError) {
        previousError = error;
        emit connectionError(error, m_serialPort.errorString());
        if (m_serialPort.isOpen()) {
            m_serialPort.close();
            emit portClosed();
        }
    }
}
