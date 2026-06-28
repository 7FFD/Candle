#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QObject>
#include <QStringList>
#include <QSettings>
#include <QDir>

class GCodeTableModel;

class FileManager : public QObject {
    Q_OBJECT
public:
    explicit FileManager(QObject *parent = nullptr);

    // Current document state
    const QString &programFileName()   const { return m_programFileName; }
    const QString &heightMapFileName() const { return m_heightMapFileName; }
    const QString &lastFolder()        const { return m_lastFolder; }
    bool isFileChanged()      const { return m_fileChanged; }
    bool isHeightMapChanged() const { return m_heightMapChanged; }

    void setProgramFileName(const QString &fn);
    void clearProgramFileName();
    void setHeightMapFileName(const QString &fn);
    void clearHeightMapFileName();
    void setLastFolder(const QString &folder)  { m_lastFolder = folder; }
    void setFileChanged(bool v)                { m_fileChanged = v; }
    void setHeightMapChanged(bool v)           { m_heightMapChanged = v; }

    // Recent files
    const QStringList &recentFiles()      const { return m_recentFiles; }
    const QStringList &recentHeightmaps() const { return m_recentHeightmaps; }
    void addRecentFile(const QString &fn);
    void addRecentHeightmap(const QString &fn);
    void clearRecentFiles();
    void clearRecentHeightmaps();

    // File type detection
    static bool isGCodeFile(const QString &fn);
    static bool isHeightmapFile(const QString &fn);

    // File I/O
    QStringList readGCodeFile(const QString &fileName);
    bool saveGCodeFile(const QString &fileName, GCodeTableModel *model);

    // Settings persistence
    void loadSettings(QSettings &s);
    void saveSettings(QSettings &s) const;

private:
    static const int MAX_RECENT = 5;

    QString m_programFileName;
    QString m_heightMapFileName;
    QString m_lastFolder;
    bool m_fileChanged      = false;
    bool m_heightMapChanged = false;
    QStringList m_recentFiles;
    QStringList m_recentHeightmaps;
};

#endif // FILEMANAGER_H
