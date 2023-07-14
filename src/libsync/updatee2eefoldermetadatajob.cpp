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

#include "updatee2eefoldermetadatajob.h"

#include "account.h"
#include "clientsideencryption.h"
#include "foldermetadata.h"

#include <QLoggingCategory>
#include <QNetworkReply>

namespace OCC {

Q_LOGGING_CATEGORY(lcUpdateFileDropMetadataJob, "nextcloud.sync.propagator.updatee2eefoldermetadatajob", QtInfoMsg)

}

namespace OCC {

UpdateE2eeFolderMetadataJob::UpdateE2eeFolderMetadataJob(OwncloudPropagator *propagator, const SyncFileItemPtr &item, const QString &encryptedRemotePath)
    : PropagatorJob(propagator),
    _item(item),
    _encryptedRemotePath(encryptedRemotePath)
{
}

void UpdateE2eeFolderMetadataJob::start()
{
    Q_ASSERT(_item);
    qCDebug(lcUpdateFileDropMetadataJob) << "Folder is encrypted, let's fetch metadata.";

    SyncJournalFileRecord rec;
    if (!propagator()->_journal->getRootE2eFolderRecord(_encryptedRemotePath, &rec) || !rec.isValid()) {
        unlockFolder(false);
        return;
    }
    _fetchAndUploadE2eeFolderMetadataJob.reset(
            new EncryptedFolderMetadataHandler(propagator()->account(), _encryptedRemotePath, propagator()->_journal, rec.path()));

    connect(_fetchAndUploadE2eeFolderMetadataJob.data(), &EncryptedFolderMetadataHandler::fetchFinished,
            this, &UpdateE2eeFolderMetadataJob::slotFetchMetadataJobFinished);
    _fetchAndUploadE2eeFolderMetadataJob->fetchMetadata(true);
}

bool UpdateE2eeFolderMetadataJob::scheduleSelfOrChild()
{
    if (_state == Finished) {
        return false;
    }

    if (_state == NotYetStarted) {
        _state = Running;
        start();
    }

    return true;
}

PropagatorJob::JobParallelism UpdateE2eeFolderMetadataJob::parallelism() const
{
    return PropagatorJob::JobParallelism::WaitForFinished;
}

void UpdateE2eeFolderMetadataJob::slotFetchMetadataJobFinished(int httpReturnCode, const QString &message)
{
    if (httpReturnCode != 200) {
        qCDebug(lcUpdateFileDropMetadataJob()) << "Error Getting the encrypted metadata.";
        _item->_status = SyncFileItem::FatalError;
        _item->_errorString = message;
        finished(SyncFileItem::FatalError);
        return;
    }

    SyncJournalFileRecord rec;
    if (!propagator()->_journal->getRootE2eFolderRecord(_encryptedRemotePath, &rec) || !rec.isValid()) {
        unlockFolder(false);
        return;
    }

    const auto folderMetadata = _fetchAndUploadE2eeFolderMetadataJob->folderMetadata();
    if (!folderMetadata || !folderMetadata->isValid() || (!folderMetadata->moveFromFileDropToFiles() && !folderMetadata->encryptedMetadataNeedUpdate())) {
        unlockFolder(false);
        return;
    }

    emit fileDropMetadataParsedAndAdjusted(folderMetadata.data());
    _fetchAndUploadE2eeFolderMetadataJob->uploadMetadata();
    connect(_fetchAndUploadE2eeFolderMetadataJob.data(), &EncryptedFolderMetadataHandler::uploadFinished,
            this, &UpdateE2eeFolderMetadataJob::slotUpdateMetadataFinished);
}

void UpdateE2eeFolderMetadataJob::slotUpdateMetadataFinished(int httpReturnCode, const QString &message)
{
    const auto itemStatus = httpReturnCode != 200 ? SyncFileItem::FatalError : SyncFileItem::Success;
    if (httpReturnCode != 200) {
        _item->_errorString = message;
        qCDebug(lcUpdateFileDropMetadataJob) << "Update metadata error for folder" << _fetchAndUploadE2eeFolderMetadataJob->folderId() << "with error" << httpReturnCode << message;
    } else {
        qCDebug(lcUpdateFileDropMetadataJob) << "Uploading of the metadata success, Encrypting the file";
    }
    _item->_status = itemStatus;
    finished(itemStatus);
}

void UpdateE2eeFolderMetadataJob::unlockFolder(bool success)
{
    Q_ASSERT(!_fetchAndUploadE2eeFolderMetadataJob->isUnlockRunning());
    Q_ASSERT(_item);

    if (_fetchAndUploadE2eeFolderMetadataJob->isUnlockRunning()) {
        qCWarning(lcUpdateFileDropMetadataJob) << "Double-call to unlockFolder.";
        return;
    }

    if (!success) {
        _item->_errorString = tr("Failed to update folder metadata.");
    }

    const auto itemStatus = success ? SyncFileItem::Success : SyncFileItem::FatalError;

    if (!_fetchAndUploadE2eeFolderMetadataJob->isFolderLocked()) {
        if (success && _fetchAndUploadE2eeFolderMetadataJob->folderMetadata()) {
            _item->_e2eEncryptionStatus = _fetchAndUploadE2eeFolderMetadataJob->folderMetadata()->encryptedMetadataEncryptionStatus();
            if (_item->isEncrypted()) {
                _item->_e2eEncryptionMaximumAvailableStatus = EncryptionStatusEnums::fromEndToEndEncryptionApiVersion(propagator()->account()->capabilities().clientSideEncryptionVersion());
            }
        }
        finished(itemStatus);
        return;
    }

    qCDebug(lcUpdateFileDropMetadataJob) << "Calling Unlock";
    connect(_fetchAndUploadE2eeFolderMetadataJob.data(), &EncryptedFolderMetadataHandler::folderUnlocked, [this](const QByteArray &folderId, int httpStatus) {
        qCWarning(lcUpdateFileDropMetadataJob) << "Unlock Error";

        if (httpStatus != 200) {
            _item->_errorString = tr("Failed to unlock encrypted folder.");
            finished(SyncFileItem::FatalError);
            return;
        }

        qCDebug(lcUpdateFileDropMetadataJob) << "Successfully Unlocked";

        if (!_fetchAndUploadE2eeFolderMetadataJob->folderMetadata()
            || !_fetchAndUploadE2eeFolderMetadataJob->folderMetadata()->isValid()) {
            qCWarning(lcUpdateFileDropMetadataJob) << "Failed to finalize item. Invalid metadata.";
            _item->_errorString = tr("Failed to finalize item.");
            finished(SyncFileItem::FatalError);
            return;
        }

        _item->_e2eEncryptionStatus = _fetchAndUploadE2eeFolderMetadataJob->folderMetadata()->encryptedMetadataEncryptionStatus();
        _item->_e2eEncryptionStatusRemote = _fetchAndUploadE2eeFolderMetadataJob->folderMetadata()->encryptedMetadataEncryptionStatus();

        finished(SyncFileItem::Success);
    });
    _fetchAndUploadE2eeFolderMetadataJob->unlockFolder(success);
}

}