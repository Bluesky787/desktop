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
#include "foldermetadata.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSslCertificate>
#include <QString>

namespace OCC {
class SyncJournalDb;
class OWNCLOUDSYNC_EXPORT FetchAndUploadE2eeFolderMetadataJob : public QObject
{
    Q_OBJECT

public:
    explicit FetchAndUploadE2eeFolderMetadataJob(const AccountPtr &account,
                                                 const QString &folderPath,
                                                 SyncJournalDb *const journalDb,
                                                 const QString &pathForTopLevelFolder,
                                                 QObject *parent = nullptr);

    [[nodiscard]] QSharedPointer<FolderMetadata> folderMetadata() const;
    void setMetadata(const QSharedPointer<FolderMetadata> &metadata);

    void setFolderId(const QByteArray &folderId);
    [[nodiscard]] const QByteArray folderId() const;

    void setFolderToken(const QByteArray &token); // use this when modifying metadata for multiple folders inside top-level one which is locked
    [[nodiscard]] const QByteArray folderToken() const;

    [[nodiscard]] const bool isUnlockRunning() const;
    [[nodiscard]] const bool isFolderLocked() const;

    void fetchMetadata(const FolderMetadata::RootEncryptedFolderInfo &rootEncryptedFolderInfo, bool allowEmptyMetadata = false);
    void fetchMetadata(bool allowEmptyMetadata = false);
    void uploadMetadata(bool keepLock = false);
    void unlockFolder(bool success = true);

private:
    void lockFolder();
    void startUploadMetadata();
    void startFetchMetadata();
    void fetchFolderEncryptedId();

private slots:
    void slotFolderEncryptedIdReceived(const QStringList &list);
    void slotFolderEncryptedIdError(QNetworkReply *reply);

    void slotMetadataReceived(const QJsonDocument &json, int statusCode);
    void slotMetadataReceivedError(const QByteArray &folderId, int httpReturnCode);

    void slotFolderLockedSuccessfully(const QByteArray &folderId, const QByteArray &token);
    void slotFolderLockedError(const QByteArray &folderId, int httpErrorCode);

    void slotUploadMetadataSuccess(const QByteArray &folderId);
    void slotUploadMetadataError(const QByteArray &folderId, int httpReturnCode);

    void slotEmitUploadSuccess();
    void slotEmitUploadError();

public: signals:
    void fetchFinished(int code, const QString &message = {});
    void uploadFinished(int code, const QString &message = {});
    void folderUnlocked(const QByteArray &folderId, int httpStatus);

private:
    AccountPtr _account;
    QString _folderPath;
    QPointer<SyncJournalDb> _journalDb;
    QByteArray _folderId;
    QByteArray _folderToken;

    QSharedPointer<FolderMetadata> _folderMetadata;

    FolderMetadata::RootEncryptedFolderInfo _rootEncryptedFolderInfo;

    int _uploadErrorCode = 200;

    bool _allowEmptyMetadata = false;
    bool _isFolderLocked = false;
    bool _isUnlockRunning = false;
    bool _isNewMetadataCreated = false;
    bool _keepLockedAfterUpdate = false;
};

}
