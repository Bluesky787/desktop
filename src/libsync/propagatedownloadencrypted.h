#ifndef PROPAGATEDOWNLOADENCRYPTED_H
#define PROPAGATEDOWNLOADENCRYPTED_H

#include <QObject>
#include <QFileInfo>

#include "syncfileitem.h"
#include "owncloudpropagator.h"
#include "clientsideencryption.h"
#include "foldermetadata.h"

class QJsonDocument;

namespace OCC {
class FetchAndUploadE2eeFolderMetadataJob;
class PropagateDownloadEncrypted : public QObject {
  Q_OBJECT
public:
  PropagateDownloadEncrypted(OwncloudPropagator *propagator, const QString &localParentPath, SyncFileItemPtr item, QObject *parent = nullptr);
  void start();
  bool decryptFile(QFile& tmpFile);
  [[nodiscard]] QString errorString() const;

private slots:
  void slotFetchMetadataJobFinished(int statusCode, const QString &message);

signals:
  void fileMetadataFound();
  void failed();

  void decryptionFinished();

private:
  OwncloudPropagator *_propagator;
  QString _localParentPath;
  SyncFileItemPtr _item;
  QFileInfo _info;
  FolderMetadata::EncryptedFile _encryptedInfo;
  QString _errorString;
  QString _remoteParentPath;
  QString _parentPathInDb;
  // TODO: Create a base class with this member in 'protected' and virtual slots for fetch/upload, then refactor this and other similar classes to inherit the base class
  QScopedPointer<FetchAndUploadE2eeFolderMetadataJob> _fetchAndUploadE2eeFolderMetadataJob;
};

}
#endif
