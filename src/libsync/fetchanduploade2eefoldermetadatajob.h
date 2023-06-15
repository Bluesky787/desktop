/*
 * Copyright (C) 2023 by Oleksandr Zolotov <alex@nextcloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#pragma once

#include "account.h"
#include "gui/sharemanager.h"
#include "syncfileitem.h"
#include "gui/sharee.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSslCertificate>
#include <QString>

namespace OCC {
class FolderMetadata;
class SyncJournalDb;
class OWNCLOUDSYNC_EXPORT FetchAndUploadE2eeFolderMetadataJob : public QObject
{
    Q_OBJECT

public:
    explicit FetchAndUploadE2eeFolderMetadataJob(const AccountPtr &account,
                                                 const QString &folderPath,
                                                 SyncJournalDb *journalDb,
                                                 const QString &currentPath,
                                                 const QString &possibleRootPath,
                                                 QObject *parent = nullptr);

    void setFolderMetadata(const QSharedPointer<FolderMetadata> &folderMetadata);
    QSharedPointer<FolderMetadata> folderMetadata() const;

public:
    void fetchMetadata(bool allowEmptyMetadata = false);
    void uploadMetadata();

private slots:
    void slotFetchFolderMetadata();
    void slotFetchFolderEncryptedId();
    void slotFolderEncryptedIdReceived(const QStringList &list);
    void slotFolderEncryptedIdError(QNetworkReply *reply);
    void slotMetadataReceived(const QJsonDocument &json, int statusCode);
    void slotMetadataReceivedError(const QByteArray &folderId, int httpReturnCode);
    void slotFolderLockedSuccessfully(const QByteArray &folderId, const QByteArray &token);
    void slotFolderLockedError(const QByteArray &folderId, int httpErrorCode);
    void slotLockFolder();
    void slotUnlockFolder();
    void slotUploadMetadata(bool firstUpload);
    void slotUploadMetadataSuccess(const QByteArray &folderId);
    void slotUploadMetadataError(const QByteArray &folderId, int httpReturnCode);
    void slotFolderUnlocked(const QByteArray &folderId, int httpStatus);
    void slotEmitUploadSuccess();
    void slotEmitUploadError();

public: signals:
    void fetchFinished(int code, const QString &message = {});
    void uploadFinished(int code, const QString &message = {});

private: signals:
    void folderUnlocked(const QByteArray &folderId, int httpStatus);

private:
    AccountPtr _account;
    QString _folderPath;
    QPointer<SyncJournalDb> _journalDb;
    QString _currentPathForTopLevelFolder;
    QString _possibleRootPath;
    QByteArray _folderId;
    QByteArray _folderToken;
    QSharedPointer<FolderMetadata> _folderMetadata;
    bool _allowEmptyMetadata = false;
    bool _isUnlockRunning = false;
};

}
