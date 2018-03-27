/****************************************************************************
** QWebDAV Library (qwebdavlib) - LGPL v2.1
**
** HTTP Extensions for Web Distributed Authoring and Versioning (WebDAV)
** from June 2007
**      http://tools.ietf.org/html/rfc4918
**
** Web Distributed Authoring and Versioning (WebDAV) SEARCH
** from November 2008
**      http://tools.ietf.org/html/rfc5323
**
** Missing:
**      - LOCK support
**      - process WebDAV SEARCH responses
**
** Copyright (C) 2012 Martin Haller <martin.haller@rebnil.com>
** for QWebDAV library (qwebdavlib) version 1.0
**      https://github.com/mhaller/qwebdavlib
**
** Copyright (C) 2012 Timo Zimmermann <meedav@timozimmermann.de>
** for portions from QWebdav plugin for MeeDav (LGPL v2.1)
**      http://projects.developer.nokia.com/meedav/
**
** Copyright (C) 2009-2010 Corentin Chary <corentin.chary@gmail.com>
** for portions from QWebdav - WebDAV lib for Qt4 (LGPL v2.1)
**      http://xf.iksaif.net/dev/qwebdav.html
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** for naturalCompare() (LGPL v2.1)
**      http://qt.gitorious.org/qt/qt/blobs/4.7/src/gui/dialogs/qfilesystemmodel.cpp
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Library General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Library General Public License for more details.
**
** You should have received a copy of the GNU Library General Public License
** along with this library; see the file COPYING.LIB.  If not, write to
** the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
** Boston, MA 02110-1301, USA.
**
** http://www.gnu.org/licenses/lgpl-2.1-standalone.html
**
****************************************************************************/

#ifndef QWEBDAV_H
#define QWEBDAV_H

#include <QtCore>
#include <QtNetwork>

#include "qwebdavshared_export.h"

/**
 * @brief Main class used to handle the webdav protocol
 */
class QWEBDAVSHARED_EXPORT QWebdav : public QNetworkAccessManager
{
    Q_OBJECT

public:
    enum QWebdavConnectionType {HTTP = 1, HTTPS};

    QWebdav(QObject* parent = 0);
    ~QWebdav();

    typedef QMap<QString, QMap < QString, QVariant > > PropValues;
    typedef QMap<QString, QStringList > PropNames;


    QString hostname() const;
    int port() const;
    QString rootPath() const;
    QString username() const;
    QString password() const;
    QWebdavConnectionType connectionType() const;
    bool isSSL() const;

    void setConnectionSettings( const QWebdavConnectionType connectionType,
                            const QString &hostname,
                            const QString &rootPath = "/",
                            const QString &username = "",
                            const QString &password = "",
                            int port = 0,
                            bool ignoreSslErrors = true);

    QNetworkReply* list(const QString& path);
    QNetworkReply* list(const QString& path, int depth);

    QNetworkReply* search(const QString& path, const QString& query);

    QNetworkReply* get(const QString& path, const QMap<QByteArray, QByteArray> &headers);
    QNetworkReply* get(const QString& path, QIODevice* data);
    QNetworkReply* get(const QString& path, QIODevice* data, quint64 fromRangeInBytes);

    QNetworkReply* put(const QString& path, QIODevice* data);
    QNetworkReply* put(const QString& path, const QByteArray& data, const QMap<QByteArray, QByteArray> &headers);

    QNetworkReply* mkdir(const QString& dir );
    // The extended MKCOL used in CardDAV
    QNetworkReply* mkdir(const QString& dir, const QByteArray& query);
    QNetworkReply* mkcalendar(const QString& dir, const QByteArray& query);
    QNetworkReply* copy(const QString& pathFrom, const QString& pathTo, bool overwrite = false);
    QNetworkReply* move(const QString& pathFrom, const QString& pathTo, bool overwrite = false);
    QNetworkReply* remove(const QString& path );

    QNetworkReply* propfind(const QString& path, const QByteArray& query, int depth = 0);
    QNetworkReply* propfind(const QString& path, const QWebdav::PropNames& props, int depth = 0);

    QNetworkReply* report(const QString& path, const QByteArray& query, int depth = 0);

    QNetworkReply* proppatch(const QString& path, const QWebdav::PropValues& props);
    QNetworkReply* proppatch(const QString& path, const QByteArray& query);

Q_SIGNALS:
    void errorChanged(QString error);

protected Q_SLOTS:
    void replyReadyRead();
    void replyFinished(QNetworkReply*);
    void replyDeleteLater(QNetworkReply*);
    void replyError(QNetworkReply::NetworkError);
    void provideAuthenication(QNetworkReply* reply, QAuthenticator* authenticator);
    void sslErrors(QNetworkReply *reply,const QList<QSslError> &errors);

protected:
    QNetworkReply* createDAVRequest(const QString& method, QNetworkRequest& req, QIODevice* outgoingData = 0 );
    QNetworkReply* createDAVRequest(const QString& method, QNetworkRequest& req, const QByteArray& outgoingData);

    //! creates the absolute path from m_rootPath and relPath
    QString absolutePath(const QString &relPath);

private:
    QMap<QNetworkReply*, QIODevice*> m_outDataDevices;
    QMap<QNetworkReply*, QIODevice*> m_inDataDevices;

    QString m_rootPath;
    QString m_username;
    QString m_password;
    QUrl m_baseUrl;

    QWebdavConnectionType m_currentConnectionType;

    QNetworkReply *m_authenticator_lastReply;

    bool m_ignoreSslErrors;
};

#endif // QWEBDAV_H