/*
    Copyright (c) 2010 Tobias Koenig <tokoe@kde.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#ifndef KDAV_DAVITEMCREATEJOB_H
#define KDAV_DAVITEMCREATEJOB_H

#include "libkdav_export.h"

#include "davitem.h"
#include "davjobbase.h"
#include "davurl.h"

namespace KDAV
{

/**
 * @short A job to create a DAV item on the DAV server.
 */
class LIBKDAV_EXPORT DavItemCreateJob : public DavJobBase
{
    Q_OBJECT

public:
    /**
     * Creates a new dav item create job.
     *
     * @param url The target url where the item shall be created.
     * @param item The item that shall be created.
     * @param parent The parent object.
     */
    DavItemCreateJob(const DavUrl &url, const DavItem &item, QObject *parent = Q_NULLPTR);

    /**
     * Starts the job.
     */
    void start() Q_DECL_OVERRIDE;

    /**
     * Returns the created DAV item including the correct identifier url
     * and current etag information.
     */
    DavItem item() const;

private Q_SLOTS:
    void davJobFinished(KJob *);
    void itemRefreshed(KJob *);

private:
    DavUrl mUrl;
    DavItem mItem;
    int mRedirectCount;
};

}

#endif