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

#include "owncloudpropagator.h"

#include <QScopedPointer>

class QNetworkReply;

namespace OCC {

class FolderMetadata;

class OWNCLOUDSYNC_EXPORT UpdateMigratedE2eeMetadataJob : public PropagatorJob
{
    Q_OBJECT

public:
    explicit UpdateMigratedE2eeMetadataJob(OwncloudPropagator *propagator, const QByteArray &folderId, const QString &path, const QString &folderRemotePath);

    bool scheduleSelfOrChild() override;

    [[nodiscard]] JobParallelism parallelism() const override;

private slots:
    void start();

private:
    QByteArray _folderId;
    QString _path;
    QString _folderRemotePath;
};

}
