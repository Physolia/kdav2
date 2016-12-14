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

#ifndef KDAV_DAVITEMDELETEJOB_H
#define KDAV_DAVITEMDELETEJOB_H

#include "libkdav_export.h"

#include "davitem.h"
#include "davjobbase.h"
#include "davutils.h"

namespace KDAV
{

/**
 * @short A job to delete a DAV item on the DAV server.
 */
class LIBKDAV_EXPORT DavItemDeleteJob : public DavJobBase
{
    Q_OBJECT

public:
    /**
     * Creates a new dav item delete job.
     *
     * @param url The url of the item that shall be deleted.
     * @param item The item that shall be deleted.
     * @param parent The parent object.
     */
    DavItemDeleteJob(const DavUtils::DavUrl &url, const DavItem &item, QObject *parent = Q_NULLPTR);

    /**
     * Starts the job.
     */
    void start() Q_DECL_OVERRIDE;

    /**
     * Returns the item that triggered the conflict, if any.
     */
    DavItem freshItem() const;

    /**
     * Returns the response code we got when fetching the fresh item.
     */
    int freshResponseCode() const;

private Q_SLOTS:
    void davJobFinished(KJob *);
    void conflictingItemFetched(KJob *);

private:
    DavUtils::DavUrl mUrl;
    DavItem mItem;
    DavItem mFreshItem;
    int mFreshResponseCode;
};

}

#endif
