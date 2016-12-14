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

#ifndef KDAV_DAVITEMFETCHJOB_H
#define KDAV_DAVITEMFETCHJOB_H

#include "libkdav_export.h"

#include "davitem.h"
#include "davjobbase.h"
#include "davutils.h"

/**
 * @short A job that fetches a DAV item from the DAV server.
 */
class LIBKDAV_EXPORT DavItemFetchJob : public DavJobBase
{
    Q_OBJECT

public:
    /**
     * Creates a new dav item fetch job.
     *
     * @param url The DAV url of the item that shall be fetched.
     * @param item The item that shall be fetched.
     * @param parent The parent object.
     */
    DavItemFetchJob(const DavUtils::DavUrl &url, const DavItem &item, QObject *parent = Q_NULLPTR);

    /**
     * Starts the job.
     */
    void start() Q_DECL_OVERRIDE;

    /**
     * Returns the fetched item including current etag information.
     */
    DavItem item() const;

private Q_SLOTS:
    void davJobFinished(KJob *);

private:
    DavUtils::DavUrl mUrl;
    DavItem mItem;
};

#endif
