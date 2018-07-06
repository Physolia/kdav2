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

#include "davcollectionsfetchjob.h"

#include "davmanager.h"
#include "davprincipalhomesetsfetchjob.h"
#include "davprotocolbase.h"
#include "utils.h"
#include "daverror.h"
#include "davjob.h"

#include "libkdav2_debug.h"

#include <QtCore/QBuffer>
#include <QtXmlPatterns/QXmlQuery>

using namespace KDAV2;

DavCollectionsFetchJob::DavCollectionsFetchJob(const DavUrl &url, QObject *parent)
    : DavJobBase(parent), mUrl(url), mSubJobCount(0)
{
}

void DavCollectionsFetchJob::start()
{
    if (DavManager::self()->davProtocol(mUrl.protocol())->supportsPrincipals()) {
        DavPrincipalHomeSetsFetchJob *job = new DavPrincipalHomeSetsFetchJob(mUrl);
        connect(job, &DavPrincipalHomeSetsFetchJob::result, this, &DavCollectionsFetchJob::principalFetchFinished);
        job->start();
    } else {
        doCollectionsFetch(mUrl.url());
    }
}

DavCollection::List DavCollectionsFetchJob::collections() const
{
    return mCollections;
}

DavUrl DavCollectionsFetchJob::davUrl() const
{
    return mUrl;
}

void DavCollectionsFetchJob::doCollectionsFetch(const QUrl &url)
{
    ++mSubJobCount;

    const QDomDocument collectionQuery = DavManager::self()->davProtocol(mUrl.protocol())->collectionsQuery()->buildQuery();

    auto job = DavManager::self()->createPropFindJob(url, collectionQuery);
    connect(job, &DavJob::result, this, &DavCollectionsFetchJob::collectionsFetchFinished);
}

void DavCollectionsFetchJob::principalFetchFinished(KJob *job)
{
    const DavPrincipalHomeSetsFetchJob *davJob = qobject_cast<DavPrincipalHomeSetsFetchJob *>(job);

    if (davJob->error()) {
        if (davJob->latestResponseCode()) {
            // If we have a HTTP response code then this may mean that
            // the URL was not a principal URL. Retry as if it were a calendar URL.
            qCDebug(KDAV2_LOG) << "Principal fetch failed, retrying: " << job->errorText();
            doCollectionsFetch(mUrl.url());
        } else {
            // Just give up here.
            setDavError(davJob->davError());
            setErrorTextFromDavError();
            emitResult();
        }

        return;
    }

    const QStringList homeSets = davJob->homeSets();
    qCDebug(KDAV2_LOG) << "Found " << homeSets.size() << " homesets";
    qCDebug(KDAV2_LOG) << homeSets;

    if (homeSets.isEmpty()) {
        // Same as above, retry as if it were a calendar URL.
        doCollectionsFetch(mUrl.url());
        return;
    }

    foreach (const QString &homeSet, homeSets) {
        QUrl url = mUrl.url();

        if (homeSet.startsWith(QLatin1Char('/'))) {
            // homeSet is only a path, use request url to complete
            url.setPath(homeSet, QUrl::TolerantMode);
        } else {
            // homeSet is a complete url
            QUrl tmpUrl(homeSet);
            tmpUrl.setUserName(url.userName());
            tmpUrl.setPassword(url.password());
            url = tmpUrl;
        }

        doCollectionsFetch(url);
    }
}

void DavCollectionsFetchJob::collectionsFetchFinished(KJob *job)
{
    auto davJob = qobject_cast<DavJob *>(job);
    const int responseCode = davJob->responseCode();

    if (davJob->error()) {
        if (davJob->url() != mUrl.url()) {
            // Retry as if the initial URL was a calendar URL.
            // We can end up here when retrieving a homeset on
            // which a PROPFIND resulted in an error
            doCollectionsFetch(mUrl.url());
            --mSubJobCount;
            return;
        }

        setLatestResponseCode(responseCode);
        setError(ERR_PROBLEM_WITH_REQUEST);
        setJobErrorText(davJob->errorText());
        setJobError(davJob->error());
        setErrorTextFromDavError();
    } else {
        // For use in the collectionDiscovered() signal
        QUrl _jobUrl = mUrl.url();
        _jobUrl.setUserInfo(QString());
        const QString jobUrl = _jobUrl.toDisplayString();

        // Validate that we got a valid PROPFIND response
        QDomElement rootElement = davJob->response().documentElement();
        if (rootElement.localName().compare(QStringLiteral("multistatus"), Qt::CaseInsensitive) != 0) {
            setError(ERR_COLLECTIONFETCH);
            setErrorTextFromDavError();
            subjobFinished();
            return;
        }

        QByteArray resp(davJob->response().toByteArray());
        QBuffer buffer(&resp);
        buffer.open(QIODevice::ReadOnly);

        QXmlQuery xquery;
        if (!xquery.setFocus(&buffer)) {
            setError(ERR_COLLECTIONFETCH_XQUERY_SETFOCUS);
            setErrorTextFromDavError();
            subjobFinished();
            return;
        }

        xquery.setQuery(DavManager::self()->davProtocol(mUrl.protocol())->collectionsXQuery());
        if (!xquery.isValid()) {
            setError(ERR_COLLECTIONFETCH_XQUERY_INVALID);
            setErrorTextFromDavError();
            subjobFinished();
            return;
        }

        QString responsesStr;
        xquery.evaluateTo(&responsesStr);
        responsesStr.prepend(QStringLiteral("<responses>"));
        responsesStr.append(QStringLiteral("</responses>"));

        QDomDocument document;
        if (!document.setContent(responsesStr, true)) {
            setError(ERR_COLLECTIONFETCH);
            setErrorTextFromDavError();
            subjobFinished();
            return;
        }

        if (!error()) {
            /*
             * Extract information from a document like the following:
             *
             * <responses>
             *   <response xmlns="DAV:">
             *     <href xmlns="DAV:">/caldav.php/test1.user/home/</href>
             *     <propstat xmlns="DAV:">
             *       <prop xmlns="DAV:">
             *         <C:supported-calendar-component-set xmlns:C="urn:ietf:params:xml:ns:caldav">
             *           <C:comp xmlns:C="urn:ietf:params:xml:ns:caldav" name="VEVENT"/>
             *           <C:comp xmlns:C="urn:ietf:params:xml:ns:caldav" name="VTODO"/>
             *           <C:comp xmlns:C="urn:ietf:params:xml:ns:caldav" name="VJOURNAL"/>
             *           <C:comp xmlns:C="urn:ietf:params:xml:ns:caldav" name="VTIMEZONE"/>
             *           <C:comp xmlns:C="urn:ietf:params:xml:ns:caldav" name="VFREEBUSY"/>
             *         </C:supported-calendar-component-set>
             *         <resourcetype xmlns="DAV:">
             *           <collection xmlns="DAV:"/>
             *           <C:calendar xmlns:C="urn:ietf:params:xml:ns:caldav"/>
             *           <C:schedule-calendar xmlns:C="urn:ietf:params:xml:ns:caldav"/>
             *         </resourcetype>
             *         <displayname xmlns="DAV:">Test1 User</displayname>
             *         <current-user-privilege-set xmlns="DAV:">
             *           <privilege xmlns="DAV:">
             *             <read xmlns="DAV:"/>
             *           </privilege>
             *         </current-user-privilege-set>
             *         <getctag xmlns="http://calendarserver.org/ns/">12345</getctag>
             *       </prop>
             *       <status xmlns="DAV:">HTTP/1.1 200 OK</status>
             *     </propstat>
             *   </response>
             * </responses>
             */

            const QDomElement responsesElement = document.documentElement();

            QDomElement responseElement = Utils::firstChildElementNS(
                responsesElement, QStringLiteral("DAV:"), QStringLiteral("response"));
            while (!responseElement.isNull()) {

                DavCollection collection;
                if (!Utils::extractCollection(responseElement, mUrl, collection)) {
                    continue;
                }

                QUrl url = collection.url().url();

                // don't add this resource if it has already been detected
                bool alreadySeen = false;
                foreach (const DavCollection &seen, mCollections) {
                    if (seen.url().toDisplayString() == url.toDisplayString()) {
                        alreadySeen = true;
                    }
                }
                if (alreadySeen) {
                    responseElement = Utils::nextSiblingElementNS(
                        responseElement, QStringLiteral("DAV:"), QStringLiteral("response"));
                    continue;
                }

                mCollections << collection;
                Q_EMIT collectionDiscovered(mUrl.protocol(), url.toDisplayString(), jobUrl);

                responseElement = Utils::nextSiblingElementNS(
                    responseElement, QStringLiteral("DAV:"), QStringLiteral("response"));
            }
        }
    }

    subjobFinished();
}

void DavCollectionsFetchJob::subjobFinished()
{
    if (--mSubJobCount == 0) {
        emitResult();
    }
}

