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

#include "fetchanduploade2eefoldermetadatajob.h"

#include "account.h"
#include "common/syncjournaldb.h"
#include "clientsideencryptionjobs.h"
#include "clientsideencryption.h"
#include "foldermetadata.h"

#include <QLoggingCategory>
#include <QNetworkReply>

namespace OCC {

Q_LOGGING_CATEGORY(lcFetchAndUploadE2eeFolderMetadataJob, "nextcloud.sync.propagator.fetchanduploade2eefoldermetadatajob", QtInfoMsg)

}

namespace OCC {

FetchAndUploadE2eeFolderMetadataJob::FetchAndUploadE2eeFolderMetadataJob(const AccountPtr &account,
                                                                         const QString &folderPath,
                                                                         SyncJournalDb *const journalDb,
                                                                         const QString &pathForTopLevelFolder,
                                                                         QObject *parent)
    : QObject(parent),
    _account(account),
    _folderPath(folderPath),
    _journalDb(journalDb),
    _pathForTopLevelFolder(pathForTopLevelFolder)
{
}

void FetchAndUploadE2eeFolderMetadataJob::setFolderMetadata(const QSharedPointer<FolderMetadata> &folderMetadata)
{
    _folderMetadata = folderMetadata;
}

QSharedPointer<FolderMetadata> FetchAndUploadE2eeFolderMetadataJob::folderMetadata() const
{
    return _folderMetadata;
}

const QByteArray FetchAndUploadE2eeFolderMetadataJob::folderId() const
{
    return _folderId;
}

void FetchAndUploadE2eeFolderMetadataJob::setFolderToken(const QByteArray &token)
{
    _folderToken = token;
}

const QByteArray FetchAndUploadE2eeFolderMetadataJob::folderToken() const
{
    return _folderToken;
}

const bool FetchAndUploadE2eeFolderMetadataJob::isUnlockRunning() const
{
    return _isUnlockRunning;
}

const bool FetchAndUploadE2eeFolderMetadataJob::isFolderLocked() const
{
    return _isFolderLocked;
}

void FetchAndUploadE2eeFolderMetadataJob::fetchMetadata(bool allowEmptyMetadata)
{
    _allowEmptyMetadata = allowEmptyMetadata;
    slotFetchFolderEncryptedId();
}

void FetchAndUploadE2eeFolderMetadataJob::uploadMetadata(bool keepLock)
{
    _keepLockedAfterUpdate = keepLock;
    if (!_folderToken.isEmpty() && _isFolderLocked) {
        slotUploadMetadata();
        return;
    }
    slotLockFolder();
}

void FetchAndUploadE2eeFolderMetadataJob::unlockFolder(bool success)
{
    if (success) {
        connect(this, &FetchAndUploadE2eeFolderMetadataJob::folderUnlocked, this, &FetchAndUploadE2eeFolderMetadataJob::slotEmitUploadSuccess);
    } else {
        connect(this, &FetchAndUploadE2eeFolderMetadataJob::folderUnlocked, this, &FetchAndUploadE2eeFolderMetadataJob::slotEmitUploadError);
    }

    if (_folderToken.isEmpty()) {
        emit folderUnlocked(_folderId, 200);
        return;
    }
    slotUnlockFolder();
}

void FetchAndUploadE2eeFolderMetadataJob::slotFetchFolderMetadata()
{
    const auto job = new GetMetadataApiJob(_account, _folderId);
    connect(job, &GetMetadataApiJob::jsonReceived, this, &FetchAndUploadE2eeFolderMetadataJob::slotMetadataReceived);
    connect(job, &GetMetadataApiJob::error, this, &FetchAndUploadE2eeFolderMetadataJob::slotMetadataReceivedError);
    job->start();
}

void FetchAndUploadE2eeFolderMetadataJob::slotFetchFolderEncryptedId()
{
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Folder is encrypted, let's get the Id from it.";
    const auto job = new LsColJob(_account, _folderPath, this);
    job->setProperties({"resourcetype", "http://owncloud.org/ns:fileid"});
    connect(job, &LsColJob::directoryListingSubfolders, this, &FetchAndUploadE2eeFolderMetadataJob::slotFolderEncryptedIdReceived);
    connect(job, &LsColJob::finishedWithError, this, &FetchAndUploadE2eeFolderMetadataJob::slotFolderEncryptedIdError);
    job->start();
}

void FetchAndUploadE2eeFolderMetadataJob::slotFolderEncryptedIdReceived(const QStringList &list)
{
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Received id of folder. Fetching metadata...";
    const auto job = qobject_cast<LsColJob *>(sender());
    const auto &folderInfo = job->_folderInfos.value(list.first());
    _folderId = folderInfo.fileId;
    slotFetchFolderMetadata();
}

void FetchAndUploadE2eeFolderMetadataJob::slotFolderEncryptedIdError(QNetworkReply *reply)
{
    Q_ASSERT(reply);
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Error retrieving the Id of the encrypted folder.";
    if (!reply) {
        emit fetchFinished(-1, tr("Error fetching encrypted folder id."));
        return;
    }
    const auto errorCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    emit fetchFinished(errorCode, reply->errorString());
}

void FetchAndUploadE2eeFolderMetadataJob::slotMetadataReceived(const QJsonDocument &json, int statusCode)
{
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Metadata Received, parsing it and decrypting" << json.toVariant();
    const auto metadataFromJson = statusCode == 404
        ? QByteArray{} : json.toJson(QJsonDocument::Compact);
    const auto rootEncryptedFolderInfo =
        FolderMetadata::RootEncryptedFolderInfo(FolderMetadata::RootEncryptedFolderInfo::createRootPath(_pathForTopLevelFolder, _folderPath));

    const auto metadata(QSharedPointer<FolderMetadata>::create(_account, metadataFromJson, rootEncryptedFolderInfo));
    connect(metadata.data(), &FolderMetadata::setupComplete, this, [this, statusCode, metadata] {
        if (!metadata->isValid()) {
            qCDebug(lcFetchAndUploadE2eeFolderMetadataJob()) << "Error parsing or decrypting metadata.";
            emit fetchFinished(-1, tr("Error parsing or decrypting metadata."));
            return;
        }
        _folderMetadata = metadata;
        emit fetchFinished(200);
    });
}

void FetchAndUploadE2eeFolderMetadataJob::slotMetadataReceivedError(const QByteArray &folderId, int httpReturnCode)
{
    Q_UNUSED(folderId);
    if (_allowEmptyMetadata) {
        qCDebug(lcFetchAndUploadE2eeFolderMetadataJob()) << "Error Getting the encrypted metadata. Pretend we got empty metadata.";
        _isNewMetadataCreated = true;
        slotMetadataReceived({}, httpReturnCode);
        return;
    }
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob()) << "Error Getting the encrypted metadata.";
    emit fetchFinished(httpReturnCode, tr("Error fetching metadata."));
}

void FetchAndUploadE2eeFolderMetadataJob::slotFolderLockedSuccessfully(const QByteArray &folderId, const QByteArray &token)
{
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Folder" << folderId << "Locked Successfully for Upload, Fetching Metadata";
    _folderToken = token;
    _isFolderLocked = true;
    slotUploadMetadata();
}

void FetchAndUploadE2eeFolderMetadataJob::slotFolderLockedError(const QByteArray &folderId, int httpErrorCode)
{
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob()) << "Error locking folder" << folderId;
    emit fetchFinished(httpErrorCode, tr("Error locking folder."));
}

void FetchAndUploadE2eeFolderMetadataJob::slotLockFolder()
{
    Q_ASSERT(_isFolderLocked);
    if (_isFolderLocked) {
        qCDebug(lcFetchAndUploadE2eeFolderMetadataJob()) << "Error locking folder" << _folderId << "already locked";
        emit uploadFinished(-1, tr("Error locking folder."));
        return;
    }
    const auto lockJob = new LockEncryptFolderApiJob(_account, _folderId, _journalDb, _account->e2e()->_publicKey, this);
    connect(lockJob, &LockEncryptFolderApiJob::success, this, &FetchAndUploadE2eeFolderMetadataJob::slotFolderLockedSuccessfully);
    connect(lockJob, &LockEncryptFolderApiJob::error, this, &FetchAndUploadE2eeFolderMetadataJob::slotFolderLockedError);
    if (_account->capabilities().clientSideEncryptionVersion() >= 2.0) {
        lockJob->setCounter(_folderMetadata->newCounter());
    }
    lockJob->start();
}

void FetchAndUploadE2eeFolderMetadataJob::slotUnlockFolder()
{
    Q_ASSERT(!_isUnlockRunning);
    Q_ASSERT(_isFolderLocked);

    if (_isUnlockRunning) {
        qCWarning(lcFetchAndUploadE2eeFolderMetadataJob) << "Double-call to unlockFolder.";
        return;
    }
    if (!_isFolderLocked) {
        qCWarning(lcFetchAndUploadE2eeFolderMetadataJob) << "Folder is not locked.";
        emit folderUnlocked(_folderId, 400);
        return;
    }

    _isUnlockRunning = true;

    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Calling Unlock";

    const auto unlockJob = new UnlockEncryptFolderApiJob(_account, _folderId, _folderToken, _journalDb, this);
    connect(unlockJob, &UnlockEncryptFolderApiJob::success, [this](const QByteArray &folderId) {
        qDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Successfully Unlocked";
        _isFolderLocked = false;
        emit folderUnlocked(folderId, 200);
        _isUnlockRunning = false;
    });
    connect(unlockJob, &UnlockEncryptFolderApiJob::error, [this](const QByteArray &folderId, int httpStatus) {
        qDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Unlock Error";
        emit folderUnlocked(folderId, httpStatus);
        _isUnlockRunning = false;
    });
    unlockJob->start();
}

void FetchAndUploadE2eeFolderMetadataJob::slotUploadMetadata()
{
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Metadata created, sending to the server.";

    _uploadSignalEmitted = false;

    if (!_folderMetadata->isValid()) {
        slotUploadMetadataError(_folderId, -1);
        return;
    }

    const auto encryptedMetadata = _folderMetadata->encryptedMetadata();
    if (_isNewMetadataCreated) {
        const auto job = new StoreMetaDataApiJob(_account, _folderId, _folderToken, encryptedMetadata);
        connect(job, &StoreMetaDataApiJob::success, this, &FetchAndUploadE2eeFolderMetadataJob::slotUploadMetadataSuccess);
        connect(job, &StoreMetaDataApiJob::error, this, &FetchAndUploadE2eeFolderMetadataJob::slotUploadMetadataError);
        job->start();
    } else {
        const auto job = new UpdateMetadataApiJob(_account, _folderId, encryptedMetadata, _folderToken);
        connect(job, &UpdateMetadataApiJob::success, this, &FetchAndUploadE2eeFolderMetadataJob::slotUploadMetadataSuccess);
        connect(job, &UpdateMetadataApiJob::error, this, &FetchAndUploadE2eeFolderMetadataJob::slotUploadMetadataError);
        job->start();
    }
}

void FetchAndUploadE2eeFolderMetadataJob::slotUploadMetadataSuccess(const QByteArray &folderId)
{
    Q_UNUSED(folderId);
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Uploading of the metadata success.";
    if (_keepLockedAfterUpdate) {
        slotEmitUploadSuccess();
        return;
    }
    connect(this, &FetchAndUploadE2eeFolderMetadataJob::folderUnlocked, this, &FetchAndUploadE2eeFolderMetadataJob::slotEmitUploadSuccess);
    if (_isFolderLocked) {
        slotUnlockFolder();
    } else {
        slotEmitUploadSuccess();
    }
}

void FetchAndUploadE2eeFolderMetadataJob::slotUploadMetadataError(const QByteArray &folderId, int httpReturnCode)
{
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob) << "Update metadata error for folder" << folderId << "with error" << httpReturnCode;
    qCDebug(lcFetchAndUploadE2eeFolderMetadataJob()) << "Unlocking the folder.";
    connect(this, &FetchAndUploadE2eeFolderMetadataJob::folderUnlocked, this, &FetchAndUploadE2eeFolderMetadataJob::slotEmitUploadError);
    slotUnlockFolder();
}

void FetchAndUploadE2eeFolderMetadataJob::slotEmitUploadSuccess()
{
    if (!_uploadSignalEmitted) {
        _uploadSignalEmitted = true;
        emit uploadFinished(200);
    }
}

void FetchAndUploadE2eeFolderMetadataJob::slotEmitUploadError()
{
    emit uploadFinished(1, tr("Failed to upload metadata"));
}

}
