#pragma once
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

#include "accountfwd.h"
#include "encryptedfoldermetadatahandler.h"
#include <csync.h>
#include <rootencryptedfolderinfo.h>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QSslKey>
#include <QString>
#include <QVector>

class QSslCertificate;
class QJsonDocument;
namespace OCC
{
class OWNCLOUDSYNC_EXPORT FolderMetadata : public QObject
{
    Q_OBJECT
    // represents a user that has access to a folder for which metadata instance is created
    struct FolderUser {
        QString userId;
        QByteArray certificatePem;
        QByteArray encryptedMetadataKey;
        QByteArray encryptedFiledropKey;
    };

    // based on api-version and "version" key in metadata JSON
    enum MetadataVersion {
        VersionUndefined = -1,
        Version1,
        Version1_2,
        Version2_0,
    };

public:
    struct EncryptedFile {
        QByteArray encryptionKey;
        QByteArray mimetype;
        QByteArray initializationVector;
        QByteArray authenticationTag;
        QString encryptedFilename;
        QString originalFilename;
    };

    FolderMetadata(AccountPtr account);

    FolderMetadata(AccountPtr account,
                   const QByteArray &metadata,
                   const RootEncryptedFolderInfo &rootEncryptedFolderInfo,
                   const QByteArray &signature,
                   QObject *parent = nullptr);

    [[nodiscard]] QVector<EncryptedFile> files() const;

    [[nodiscard]] bool isValid() const;

    [[nodiscard]] bool isFileDropPresent() const;

    [[nodiscard]] bool isRootEncryptedFolder() const;

    [[nodiscard]] bool encryptedMetadataNeedUpdate() const;

    [[nodiscard]] bool moveFromFileDropToFiles();

    [[nodiscard]] const QByteArray &fileDrop() const; // for unit tests

    bool addUser(const QString &userId, const QSslCertificate &certificate); //adds a user to have access to this folder (always generates new metadata key)
    bool removeUser(const QString &userId); // removes a user from this folder and removes and generates a new metadata key

    [[nodiscard]] const QByteArray metadataKeyForEncryption() const;
    [[nodiscard]] const QByteArray metadataKeyForDecryption() const;
    [[nodiscard]] const QSet<QByteArray> &keyChecksums() const;
    [[nodiscard]] const QSet<QByteArray> &keyChecksumsRemoved() const;

    QByteArray encryptedMetadata();

    [[nodiscard]] EncryptionStatusEnums::ItemEncryptionStatus existingMetadataEncryptionStatus() const;
    [[nodiscard]] EncryptionStatusEnums::ItemEncryptionStatus encryptedMetadataEncryptionStatus() const;

    [[nodiscard]] bool isVersion2AndUp() const;

    [[nodiscard]] quint64 counter() const;
    [[nodiscard]] quint64 newCounter() const;

    [[nodiscard]] QByteArray metadataSignature() const;

    [[nodiscard]] QByteArray initialMetadata() const;

private:
    QByteArray encryptedMetadataLegacy();

    [[nodiscard]] bool verifyMetadataKey(const QByteArray &metadataKey) const;

    [[nodiscard]] QByteArray encryptDataWithPublicKey(const QByteArray &data, const QSslKey key) const;
    [[nodiscard]] QByteArray decryptDataWithPrivateKey(const QByteArray &data) const;

    [[nodiscard]] QByteArray encryptJsonObject(const QByteArray& obj, const QByteArray pass) const;
    [[nodiscard]] QByteArray decryptJsonObject(const QByteArray& encryptedJsonBlob, const QByteArray& pass) const;

    [[nodiscard]] bool checkMetadataKeyChecksum(const QByteArray &metadataKey, const QByteArray &metadataKeyChecksum) const;

    [[nodiscard]] QByteArray computeMetadataKeyChecksum(const QByteArray &metadataKey) const;

    [[nodiscard]] EncryptedFile parseEncryptedFileFromJson(const QString &encryptedFilename, const QJsonValue &fileJSON) const;

    [[nodiscard]] QJsonObject convertFileToJsonObject(const EncryptedFile *encryptedFile, const QByteArray &metadataKey) const;

    [[nodiscard]] const MetadataVersion latestSupportedMetadataVersion() const;

    static EncryptionStatusEnums::ItemEncryptionStatus fromMedataVersionToItemEncryptionStatus(const MetadataVersion &metadataVersion);
    static MetadataVersion fromItemEncryptionStatusToMedataVersion(const EncryptionStatusEnums::ItemEncryptionStatus &encryptionStatus);

    static QByteArray prepareMetadataForSignature(const QJsonDocument &fullMetadata);

public slots:
    void addEncryptedFile(const EncryptedFile &f);
    void removeEncryptedFile(const EncryptedFile &f);
    void removeAllEncryptedFiles();

private slots:
    void initMetadata();
    void initEmptyMetadata();
    void initEmptyMetadataLegacy();

    void setupExistingMetadata(const QByteArray &metadata);
    void setupExistingMetadataLegacy(const QByteArray &metadata);

    void setupVersionFromExistingMetadata(const QByteArray &metadata);

    void startFetchRootE2eeFolderMetadata(const QString &path);
    void slotRootE2eeFolderMetadataReceived(int statusCode, const QString &message);

    void updateUsersEncryptedMetadataKey();
    void createNewMetadataKeyForEncryption();

    void emitSetupComplete();

signals:
    void setupComplete();

private:
    AccountPtr _account;
    QByteArray _initialMetadata;

    bool _isRootEncryptedFolder = false;
    QByteArray _metadataKeyForEncryption;
    QByteArray _metadataKeyForDecryption; // used for storing initial metadataKey to use for decryption, especially in nested folders when changing the metadataKey and re-encrypting nested dirs
    QSet<QByteArray> _keyChecksums;
    QSet<QByteArray> _keyChecksumsRemoved;
    QByteArray _metadataNonce;

    QByteArray _fileDropMetadataNonceBase64;
    QByteArray _fileDropMetadataAuthenticationTag;
    QByteArray _fileDropKey;
    QByteArray _fileDropCipherTextEncryptedAndBase64;
    QJsonObject _fileDrop;

    // used by unit tests, must get assigned simultaneously with _fileDrop and not erased
    QJsonObject _fileDropFromServer;

    QMap<int, QByteArray> _metadataKeys; //legacy, remove after migration is done

    QHash<QString, FolderUser> _folderUsers;

    quint64 _counter = 0;

    MetadataVersion _existingMetadataVersion = MetadataVersion::VersionUndefined;
    MetadataVersion _encryptedMetadataVersion = MetadataVersion::VersionUndefined;

    QByteArray _metadataSignature;

    QByteArray _initialSignature;

    QVector<EncryptedFile> _files;

    bool _isMetadataValid = false;

    QScopedPointer<EncryptedFolderMetadataHandler> _fetchAndUploadE2eeFolderMetadataJob;
};

} // namespace OCC
