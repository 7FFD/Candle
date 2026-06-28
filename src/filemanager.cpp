#include "filemanager.h"
#include "tables/gcodetablemodel.h"

#include <QFile>
#include <QTextStream>
#include <QDir>

FileManager::FileManager(QObject *parent)
    : QObject(parent)
    , m_lastFolder(QDir::homePath())
{
}

void FileManager::setProgramFileName(const QString &fn)
{
    m_programFileName = fn;
}

void FileManager::clearProgramFileName()
{
    m_programFileName.clear();
}

void FileManager::setHeightMapFileName(const QString &fn)
{
    m_heightMapFileName = fn;
}

void FileManager::clearHeightMapFileName()
{
    m_heightMapFileName.clear();
}

void FileManager::addRecentFile(const QString &fn)
{
    m_recentFiles.removeAll(fn);
    m_recentFiles.append(fn);
    if (m_recentFiles.count() > MAX_RECENT) m_recentFiles.takeFirst();
}

void FileManager::addRecentHeightmap(const QString &fn)
{
    m_recentHeightmaps.removeAll(fn);
    m_recentHeightmaps.append(fn);
    if (m_recentHeightmaps.count() > MAX_RECENT) m_recentHeightmaps.takeFirst();
}

void FileManager::clearRecentFiles()
{
    m_recentFiles.clear();
}

void FileManager::clearRecentHeightmaps()
{
    m_recentHeightmaps.clear();
}

bool FileManager::isGCodeFile(const QString &fn)
{
    return fn.endsWith(".txt", Qt::CaseInsensitive)
        || fn.endsWith(".nc",  Qt::CaseInsensitive)
        || fn.endsWith(".ncc", Qt::CaseInsensitive)
        || fn.endsWith(".ngc", Qt::CaseInsensitive)
        || fn.endsWith(".tap", Qt::CaseInsensitive);
}

bool FileManager::isHeightmapFile(const QString &fn)
{
    return fn.endsWith(".map", Qt::CaseInsensitive);
}

QStringList FileManager::readGCodeFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QStringList lines;
    QTextStream stream(&file);
    while (!stream.atEnd()) lines.append(stream.readLine());
    return lines;
}

bool FileManager::saveGCodeFile(const QString &fileName, GCodeTableModel *model)
{
    QFile file(fileName);
    QDir dir;

    if (file.exists()) dir.remove(file.fileName());
    if (!file.open(QIODevice::WriteOnly)) return false;

    QTextStream stream(&file);
    for (int i = 0; i < model->rowCount() - 1; i++) {
        stream << model->data(model->index(i, 1)).toString() << "\r\n";
    }
    file.close();
    return true;
}

void FileManager::loadSettings(QSettings &s)
{
    m_recentFiles      = s.value("recentFiles",      QStringList()).toStringList();
    m_recentHeightmaps = s.value("recentHeightmaps", QStringList()).toStringList();
    m_lastFolder       = s.value("lastFolder",        QDir::homePath()).toString();
}

void FileManager::saveSettings(QSettings &s) const
{
    s.setValue("recentFiles",      m_recentFiles);
    s.setValue("recentHeightmaps", m_recentHeightmaps);
    s.setValue("lastFolder",       m_lastFolder);
}
