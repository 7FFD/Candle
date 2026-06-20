// This file is a part of "Candle" application.
// Copyright 2015-2016 Hayrullin Denis Ravilevich

#ifndef GRBLSERIAL_H
#define GRBLSERIAL_H

#include <QObject>
#include <QtSerialPort/QSerialPort>
#include <QTimer>
#include <QList>
#include <QVector3D>
#include <functional>

// Grbl machine status indices (match the order of STATUS_STRINGS in grblserial.cpp)
enum GrblMachineStatus {
    GRBL_UNKNOWN = 0,
    GRBL_IDLE    = 1,
    GRBL_ALARM   = 2,
    GRBL_RUN     = 3,
    GRBL_HOME    = 4,
    GRBL_HOLD0   = 5,
    GRBL_HOLD1   = 6,
    GRBL_QUEUE   = 7,
    GRBL_CHECK   = 8,
    GRBL_DOOR    = 9,
    GRBL_JOG     = 10
};

// Sent command tracked in the serial buffer
struct CommandAttributes {
    int length;
    int consoleIndex;
    int tableIndex;
    QString command;
};

// Command held in overflow queue (buffer was full when enqueue() was called)
struct CommandQueue {
    QString command;
    int tableIndex;
    bool showInConsole;
};

// Parsed grbl status response (<...> line)
struct GrblStatus {
    int machineStatus = GRBL_UNKNOWN;
    QString mposX, mposY, mposZ;
    double wcoX = 0, wcoY = 0, wcoZ = 0;
    int feedOverride = -1, rapidOverride = -1, spindleOverride = -1;
    QString pinState;
    QString accessoriesState;
    double currentFeed = 0, currentSpindle = 0;
    bool hasWco        = false;
    bool hasOverrides  = false;
    bool hasPinState   = false;
    bool hasAccessories = false;
    bool hasFeedSpeed  = false;
};

class frmSettings;

class GrblSerial : public QObject
{
    Q_OBJECT
public:
    static const int BUFFERLENGTH = 127;

    // Callback type used to append a command to the console and return its block index.
    // Only called when a queued-overflow command is finally sent.
    using ConsoleAppender = std::function<int(const QString &)>;

    explicit GrblSerial(frmSettings *settings, QObject *parent = nullptr);

    // --- Port management ---
    void setPortName(const QString &name);
    void setBaudRate(qint32 baud);
    QString portName() const;
    qint32 baudRate() const;
    bool isOpen() const;
    void openPort();
    void closePort();

    // --- Command sending ---
    // Enqueue a command. consoleIndex is the QPlainTextEdit block index where the
    // command was already printed (-1 if not printed). showInConsoleIfQueued controls
    // whether the ConsoleAppender is called when the command drains from the overflow queue.
    void enqueue(const QString &command, int tableIndex, int consoleIndex,
                 bool showInConsoleIfQueued = false);

    // Write a real-time byte directly (!, ~, 0x18, override bytes, etc.)
    void writeRaw(const QByteArray &data);

    // Perform a grbl soft-reset. consoleIndex = block index of the "[CTRL+X]" console line (-1 = not shown).
    void reset(int consoleIndex);

    // --- Buffer / queue info ---
    int bufferLength() const;
    int queueLength() const;
    bool fitsInBuffer(const QString &command) const;

    // --- State accessors ---
    bool isResetCompleted() const { return m_resetCompleted; }
    bool isStatusReceived() const { return m_statusReceived; }
    bool isReseting()       const { return m_reseting; }
    bool isHoming()         const { return m_homing; }
    int  lastGrblStatus()   const { return m_lastGrblStatus; }

    // --- State setters driven by frmMain ---
    void setHoming(bool v)  { m_homing = v; }
    void setPaused(bool v)  { m_paused = v; }

    // Schedule a spindle-speed command (S<speed>) to be sent on the next connection-timer tick.
    void requestSpindleUpdate(int speed);
    // Schedule a $G parser-status query on the next connection-timer tick.
    void requestParserUpdate();

    // Start the connection-poll timer (call once after construction).
    void startConnectionTimer() { m_timerConnection.start(1000); }

    // Stop both timers (e.g. before showing a blocking dialog).
    void stopTimers();
    // Restart both timers after a blocking dialog returns.
    void startTimers();

    // Update the state-query interval (e.g. from settings dialog or check-mode toggle).
    void setQueryInterval(int ms) { m_timerStateQuery.setInterval(ms); }

    // True if the last in-flight command is an end-program (M2/M30) command.
    bool hasEndProgramPending() const;

    // Number of in-flight commands currently in the serial buffer.
    int commandCount() const { return m_commands.count(); }

    // Shift console block indices of in-flight commands after inline text insertion.
    void shiftConsoleIndices(int after, int by);

    // Console appender: called when an overflow-queued command is finally flushed to serial.
    void setConsoleAppender(ConsoleAppender fn) { m_consoleAppender = fn; }

signals:
    void portOpened();
    void portClosed();
    void connectionError(QSerialPort::SerialPortError error, QString message);

    // Parsed grbl status line
    void statusReceived(GrblStatus status);

    // A command completed (ok/error received for it)
    void commandAcknowledged(CommandAttributes ca, QString response);

    // A response line that didn't match any queued command
    void floatingResponseReceived(QString data);

    // Grbl sent its startup/reset message spontaneously (hardware reset)
    void hardwareResetReceived();

private slots:
    void onReadyRead();
    void onSerialError(QSerialPort::SerialPortError error);
    void onTimerConnection();
    void onTimerStateQuery();

private:
    frmSettings *m_settings;
    QSerialPort  m_serialPort;

    QList<CommandAttributes> m_commands; // in-flight commands (sent to grbl, awaiting ok)
    QList<CommandQueue>      m_queue;    // overflow: commands waiting for buffer space

    // Protocol state
    bool m_reseting       = false;
    bool m_resetCompleted = true;
    bool m_statusReceived = true;
    bool m_homing         = false;
    bool m_paused         = false;
    int  m_lastGrblStatus = -1;

    // Deferred update requests (sent on next connection-timer tick)
    bool m_updateSpindleSpeed  = false;
    int  m_spindleSpeed        = 0;
    bool m_updateParserStatus  = false;

    QTimer m_timerConnection;
    QTimer m_timerStateQuery;

    ConsoleAppender m_consoleAppender;

    // Response classification helpers
    static bool dataIsEnd(const QString &data);
    static bool dataIsFloating(const QString &data);
    static bool dataIsReset(const QString &data);

    // Drain the overflow queue into the serial buffer after an ok is received
    void drainQueue();

    // Send a command that was already removed from m_queue
    void sendQueued(const CommandQueue &cq);
};

#endif // GRBLSERIAL_H
