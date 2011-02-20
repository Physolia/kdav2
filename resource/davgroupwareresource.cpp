/*
    Copyright (c) 2009 Grégory Oestreicher <greg@kamago.net>

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

#include "davgroupwareresource.h"

#include "configdialog.h"
#include "davcollectiondeletejob.h"
#include "davcollectionsfetchjob.h"
#include "davcollectionsmultifetchjob.h"
#include "davitemcreatejob.h"
#include "davitemdeletejob.h"
#include "davitemfetchjob.h"
#include "davitemmodifyjob.h"
#include "davitemsfetchjob.h"
#include "davitemslistjob.h"
#include "davmanager.h"
#include "davprotocolattribute.h"
#include "davprotocolbase.h"
#include "settings.h"
#include "settingsadaptor.h"
#include "setupwizard.h"

#include <kcalcore/incidence.h>
#include <kcalcore/icalformat.h>
#include <kcalcore/todo.h>

#include <akonadi/attributefactory.h>
#include <akonadi/cachepolicy.h>
#include <akonadi/changerecorder.h>
#include <akonadi/collectionfetchscope.h>
#include <akonadi/entitydisplayattribute.h>
#include <akonadi/itemfetchscope.h>

#include <kabc/addressee.h>
#include <kabc/vcardconverter.h>
#include <kwindowsystem.h>

#include <QtCore/QSet>
#include <QtDBus/QDBusConnection>

using namespace Akonadi;

typedef QSharedPointer<KCalCore::Incidence> IncidencePtr;

DavGroupwareResource::DavGroupwareResource( const QString &id )
  : ResourceBase( id )
{
  AttributeFactory::registerAttribute<EntityDisplayAttribute>();
  AttributeFactory::registerAttribute<DavProtocolAttribute>();

  mDavCollectionRoot.setParentCollection( Collection::root() );
  mDavCollectionRoot.setName( identifier() );
  mDavCollectionRoot.setRemoteId( identifier() );
  mDavCollectionRoot.setContentMimeTypes( QStringList() << Collection::mimeType() );

  EntityDisplayAttribute *attribute = mDavCollectionRoot.attribute<EntityDisplayAttribute>( Collection::AddIfMissing );
  attribute->setIconName( QLatin1String( "folder-remote" ) );

  int refreshInterval = Settings::self()->refreshInterval();
  if ( refreshInterval == 0 )
    refreshInterval = -1;

  Akonadi::CachePolicy cachePolicy;
  cachePolicy.setInheritFromParent( false );
  cachePolicy.setSyncOnDemand( false );
  cachePolicy.setCacheTimeout( -1 );
  cachePolicy.setIntervalCheckTime( refreshInterval );
  mDavCollectionRoot.setCachePolicy( cachePolicy );

  changeRecorder()->fetchCollection( true );
  changeRecorder()->collectionFetchScope().setAncestorRetrieval( Akonadi::CollectionFetchScope::All );
  changeRecorder()->itemFetchScope().fetchFullPayload( true );
  changeRecorder()->itemFetchScope().setAncestorRetrieval( ItemFetchScope::All );

  Settings::self()->setWinId( winIdForDialogs() );
  synchronize();
}

DavGroupwareResource::~DavGroupwareResource()
{
}

void DavGroupwareResource::collectionRemoved( const Akonadi::Collection &collection )
{
  kDebug() << "Removing collection " << collection.remoteId();

  if ( !configurationIsValid() ) {
    emit status( Broken, i18n( "The resource is not configured yet" ) );
    cancelTask( i18n( "The resource is not configured yet" ) );
    return;
  }

  const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( collection.remoteId() );

  DavCollectionDeleteJob *job = new DavCollectionDeleteJob( davUrl );
  connect( job, SIGNAL( result( KJob* ) ), SLOT( onCollectionRemovedFinished( KJob* ) ) );
  job->start();
}

void DavGroupwareResource::configure( WId windowId )
{
  Settings::self()->setWinId( windowId );

  // On the initial configuration we start the setup wizard
  if ( Settings::self()->configuredDavUrls().isEmpty() ) {
    SetupWizard wizard;

    if ( windowId )
      KWindowSystem::setMainWindow( &wizard, windowId );

    const int result = wizard.exec();
    if ( result == QDialog::Accepted ) {
      const SetupWizard::Url::List urls = wizard.urls();
      foreach ( const SetupWizard::Url &url, urls ) {
        Settings::UrlConfiguration *urlConfig = new Settings::UrlConfiguration();

        urlConfig->mUrl = url.url;
        urlConfig->mProtocol = url.protocol;
        urlConfig->mUser = url.userName;

        Settings::self()->newUrlConfiguration( urlConfig );
      }

      Settings::self()->setDisplayName( wizard.displayName() );
    }
  }

  // continue with the normal config dialog
  ConfigDialog dialog;

  if ( windowId )
    KWindowSystem::setMainWindow( &dialog, windowId );

  const int result = dialog.exec();

  if ( result == QDialog::Accepted ) {
    Settings::self()->writeConfig();
    synchronize();
    emit configurationDialogAccepted();
  } else {
    emit configurationDialogRejected();
  }
}

void DavGroupwareResource::retrieveCollections()
{
  kDebug() << "Retrieving collections list";

  if ( !configurationIsValid() ) {
    emit status( Broken, i18n( "The resource is not configured yet" ) );
    cancelTask( i18n( "The resource is not configured yet" ) );
    return;
  }

  emit status( Running, i18n( "Fetching collections" ) );

  DavCollectionsMultiFetchJob *job = new DavCollectionsMultiFetchJob( Settings::self()->configuredDavUrls() );
  connect( job, SIGNAL( result( KJob* ) ), SLOT( onRetrieveCollectionsFinished( KJob* ) ) );
  connect( job, SIGNAL( collectionDiscovered( const QString&, const QString& ) ),
           SLOT( onCollectionDiscovered( const QString&, const QString& ) ) );
  job->start();
}

void DavGroupwareResource::retrieveItems( const Akonadi::Collection &collection )
{
  kDebug() << "Retrieving items for collection " << collection.remoteId();

  if ( !configurationIsValid() ) {
    emit status( Broken, i18n( "The resource is not configured yet" ) );
    cancelTask( i18n( "The resource is not configured yet" ) );
    return;
  }

  const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( collection.remoteId() );

  if ( !davUrl.url().isValid() ) {
    kError() << "Can't find a configured URL, collection.remoteId() is " << collection.remoteId();
    // TODO: make this string translatable
    cancelTask( "Items requested for an unknown collection" );
    //Q_ASSERT_X( false, "DavGroupwareResource::retrieveItems", "Url is invalid" );
    return;
  }

  DavItemsListJob *job = new DavItemsListJob( davUrl );
  job->setProperty( "collection", QVariant::fromValue( collection ) );
  connect( job, SIGNAL( result( KJob* ) ), SLOT( onRetrieveItemsFinished( KJob* ) ) );
  job->start();
}

bool DavGroupwareResource::retrieveItem( const Akonadi::Item &item, const QSet<QByteArray>& )
{
  kDebug() << "Retrieving single item. Remote id = " << item.remoteId();

  if ( !configurationIsValid() ) {
    emit status( Broken, i18n( "The resource is not configured yet" ) );
    cancelTask( i18n( "The resource is not configured yet" ) );
    return false;
  }

  const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( item.parentCollection().remoteId(), item.remoteId() );

  DavItem davItem;
  davItem.setUrl( item.remoteId() );
  davItem.setContentType( "text/calendar" );
  davItem.setEtag( item.remoteRevision() );

  DavItemFetchJob *job = new DavItemFetchJob( davUrl, davItem );
  job->setProperty( "item", QVariant::fromValue( item ) );
  connect( job, SIGNAL( result( KJob* ) ), SLOT( onRetrieveItemFinished( KJob* ) ) );
  job->start();

  return true;
}

void DavGroupwareResource::itemAdded( const Akonadi::Item &item, const Akonadi::Collection &collection )
{
  kDebug() << "Received notification for added item. Local id = "
      << item.id() << ". Remote id = " << item.remoteId()
      << ". Collection remote id = " << collection.remoteId();

  if ( !configurationIsValid() ) {
    emit status( Broken, i18n( "The resource is not configured yet" ) );
    cancelTask( i18n( "The resource is not configured yet" ) );
    return;
  }

  const QString basePath = collection.remoteId();
  if ( basePath.isEmpty() ) {
    kError() << "Invalid remote id for collection " << collection.id() << " = " << collection.remoteId();
    cancelTask( i18n( "Invalid collection for item %1.", item.id() ) );
    return;
  }

  KUrl url;
  QByteArray rawData;
  QString mimeType;

  if ( item.hasPayload<KABC::Addressee>() ) {
    const KABC::Addressee contact = item.payload<KABC::Addressee>();

    const QString fileName = contact.uid();
    if ( fileName.isEmpty() ) {
      kError() << "Invalid contact uid";
      cancelTask( i18n( "Client did not create a UID for item %1.", item.id() ) );
      return;
    }

    url = KUrl( basePath + fileName + ".vcf" );

    const DavProtocolAttribute *protoAttr = collection.attribute<DavProtocolAttribute>();
    mimeType = DavManager::self()->davProtocol( DavUtils::Protocol( protoAttr->davProtocol() ) )->contactsMimeType();

    KABC::VCardConverter converter;
    rawData = converter.createVCard( contact );
  } else if ( item.hasPayload<IncidencePtr>() ) {
    const IncidencePtr ptr = item.payload<IncidencePtr>();

    const QString fileName = ptr->uid();
    if ( fileName.isEmpty() ) {
      kError() << "Invalid incidence uid";
      cancelTask( i18n( "Client did not create a UID for item %1.", item.id() ) );
      return;
    }

    url = KUrl( basePath + fileName + ".ics" );
    mimeType = "text/calendar";

    KCalCore::ICalFormat formatter;
    rawData = formatter.toICalString( ptr ).toUtf8();
  } else {
    kError() << "Item " << item.id() << " doesn't has a valid payload";
    cancelTask( i18n( "Unable to retrieve added item %1.", item.id() ) );
    return;
  }

  const QString urlStr = url.prettyUrl();
  kDebug() << "Item " << item.id() << " will be put to " << urlStr;

  const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( collection.remoteId(), urlStr );

  DavItem davItem;
  davItem.setUrl( urlStr );
  davItem.setContentType( mimeType );
  davItem.setData( rawData );

  DavItemCreateJob *job = new DavItemCreateJob( davUrl, davItem );
  job->setProperty( "item", QVariant::fromValue( item ) );
  connect( job, SIGNAL( result( KJob* ) ), SLOT( onItemAddedFinished( KJob* ) ) );
  job->start();
}

void DavGroupwareResource::itemChanged( const Akonadi::Item &item, const QSet<QByteArray>& )
{
  kDebug() << "Received notification for changed item. Local id = " << item.id()
      << ". Remote id = " << item.remoteId();

  if ( !configurationIsValid() ) {
    emit status( Broken, i18n( "The resource is not configured yet" ) );
    cancelTask( i18n( "The resource is not configured yet" ) );
    return;
  }

  const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( item.parentCollection().remoteId(), item.remoteId() );

  QByteArray rawData;
  QString mimeType;

  if ( item.hasPayload<KABC::Addressee>() ) {
    const KABC::Addressee contact = item.payload<KABC::Addressee>();

    KABC::VCardConverter converter;
    rawData = converter.createVCard( contact );

    mimeType = DavManager::self()->davProtocol( davUrl.protocol() )->contactsMimeType();
  } else if ( item.hasPayload<IncidencePtr>() ) {
    const IncidencePtr ptr = item.payload<IncidencePtr>();

    KCalCore::ICalFormat formatter;
    rawData = formatter.toICalString( ptr ).toUtf8();
    mimeType = "text/calendar";
  } else {
    kError() << "Item " << item.id() << " doesn't has a valid payload";
    cancelTask( i18n( "Unable to retrieve added item %1.", item.id() ) );
    return;
  }

  DavItem davItem;
  davItem.setUrl( item.remoteId() );
  davItem.setEtag( item.remoteRevision() );
  davItem.setContentType( mimeType );
  davItem.setData( rawData );

  DavItemModifyJob *job = new DavItemModifyJob( davUrl, davItem );
  job->setProperty( "item", QVariant::fromValue( item ) );
  connect( job, SIGNAL( result( KJob* ) ), SLOT( onItemChangedFinished( KJob* ) ) );
  job->start();
}

void DavGroupwareResource::itemRemoved( const Akonadi::Item &item )
{
  if ( !configurationIsValid() ) {
    emit status( Broken, i18n( "The resource is not configured yet" ) );
    cancelTask( i18n( "The resource is not configured yet" ) );
    return;
  }

  const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( item.parentCollection().remoteId(), item.remoteId() );

  DavItem davItem;
  davItem.setUrl( item.remoteId() );
  davItem.setEtag( item.remoteRevision() );

  DavItemDeleteJob *job = new DavItemDeleteJob( davUrl, davItem );
  connect( job, SIGNAL( result( KJob* ) ), SLOT( onItemRemovedFinished( KJob* ) ) );
  job->start();
}

void DavGroupwareResource::onCollectionRemovedFinished( KJob *job )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to remove collection: %1", job->errorText() ) );
    return;
  }

  changeProcessed();
}

void DavGroupwareResource::onRetrieveCollectionsFinished( KJob *job )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to retrieve collections: %1", job->errorText() ) );
    return;
  }

  const DavCollectionsMultiFetchJob *fetchJob = qobject_cast<DavCollectionsMultiFetchJob*>( job );

  Akonadi::Collection::List collections;
  collections << mDavCollectionRoot;

  const DavCollection::List davCollections = fetchJob->collections();
  QSet<QString> seenCollectionsNames;

  foreach ( const DavCollection &davCollection, davCollections ) {
    Akonadi::Collection collection;
    collection.setParentCollection( mDavCollectionRoot );
    collection.setRemoteId( davCollection.url() );
    if ( davCollection.displayName().isEmpty() ) {
      collection.setName( name() + " (" + davCollection.url() + ')' );
    } else {
      if ( seenCollectionsNames.contains( davCollection.displayName() ) ) {
        collection.setName( davCollection.displayName() + " (" + davCollection.url() + ')' );
      } else {
        collection.setName( davCollection.displayName() );
      }
    }

    QStringList mimeTypes;

    const DavCollection::ContentTypes contentTypes = davCollection.contentTypes();
    if ( contentTypes & DavCollection::Calendar )
      mimeTypes << QLatin1String( "text/calendar" );

    if ( contentTypes & DavCollection::Events )
      mimeTypes << KCalCore::Event::eventMimeType();

    if ( contentTypes & DavCollection::Todos )
      mimeTypes << KCalCore::Todo::todoMimeType();

    if ( contentTypes & DavCollection::Contacts )
      mimeTypes << KABC::Addressee::mimeType();

    if ( contentTypes & DavCollection::FreeBusy )
      mimeTypes << KCalCore::FreeBusy::freeBusyMimeType();

    if ( contentTypes & DavCollection::Journal )
      mimeTypes << KCalCore::Journal::journalMimeType();

    collection.setContentMimeTypes( mimeTypes );
    setCollectionIcon( collection /*by-ref*/ );

    DavProtocolAttribute *protoAttr = collection.attribute<DavProtocolAttribute>( Collection::AddIfMissing );
    protoAttr->setDavProtocol( davCollection.protocol() );

    mEtagCache.sync( collection );
    collections << collection;
  }

  collectionsRetrieved( collections );
}

void DavGroupwareResource::onRetrieveItemsFinished( KJob *job )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to retrieve items: %1", job->errorText() ) );
    return;
  }

  const Collection collection = job->property( "collection" ).value<Collection>();
  const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( collection.remoteId() );
  const bool protocolSupportsMultiget = DavManager::self()->davProtocol( davUrl.protocol() )->useMultiget();

  const DavItemsListJob *listJob = qobject_cast<DavItemsListJob*>( job );

  Akonadi::Item::List items;

  const DavItem::List davItems = listJob->items();
  foreach ( const DavItem &davItem, davItems ) {
    Akonadi::Item item;
    item.setRemoteId( davItem.url() );

    const QStringList contentMimeTypes = collection.contentMimeTypes();
    if ( contentMimeTypes.contains( KABC::Addressee::mimeType() ) )
      item.setMimeType( KABC::Addressee::mimeType() );
    else if ( contentMimeTypes.contains( QLatin1String( "text/calendar" ) ) )
      item.setMimeType( QLatin1String( "text/calendar" ) );
    else if ( contentMimeTypes.contains( KCalCore::Event::eventMimeType() ) )
      item.setMimeType( KCalCore::Event::eventMimeType() );
    else if ( contentMimeTypes.contains( KCalCore::Todo::todoMimeType() ) )
      item.setMimeType( KCalCore::Todo::todoMimeType() );
    else if ( contentMimeTypes.contains( KCalCore::FreeBusy::freeBusyMimeType() ) )
      item.setMimeType( KCalCore::FreeBusy::freeBusyMimeType() );
    else if ( contentMimeTypes.contains( KCalCore::Journal::journalMimeType() ) )
      item.setMimeType( KCalCore::Journal::journalMimeType() );

    if ( mEtagCache.etagChanged( item.remoteId(), davItem.etag() ) ) {
      // Only clear the payload (and therefor trigger a refetch from the backend) if we
      // do not use multiget, because in this case we fetch the complete payload
      // some lines below already.
      if ( !protocolSupportsMultiget ) {
        kDebug() << "Outdated item " << item.remoteId() << " (etag = " << davItem.etag() << ")";
        item.clearPayload();
      }
    }

    item.setRemoteRevision( davItem.etag() );

    items << item;
  }

  // If the protocol supports multiget then deviate from the expected behavior
  // and fetch all items with payload now instead of waiting for Akonadi to
  // request it item by item in retrieveItem().
  // This allows the resource to use the multiget query and let it be nice
  // to the remote server : only one request for n items instead of n requests.
  if ( protocolSupportsMultiget && !mEtagCache.changedRemoteIds().isEmpty() ) {
    DavItemsFetchJob *fetchJob = new DavItemsFetchJob( davUrl, mEtagCache.changedRemoteIds() );
    connect( fetchJob, SIGNAL( result( KJob* ) ), this, SLOT( onMultigetFinished( KJob* ) ) );
    fetchJob->setProperty( "items", QVariant::fromValue( items ) );
    fetchJob->start();

    // delay the call of itemsRetrieved() to onMultigetFinished()
  } else {
    itemsRetrieved( items );
  }
}

void DavGroupwareResource::onMultigetFinished( KJob *job )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to retrieve items: %1", job->errorText() ) );
    return;
  }

  const Akonadi::Item::List origItems = job->property( "items" ).value<Akonadi::Item::List>();
  const DavItemsFetchJob *davJob = qobject_cast<DavItemsFetchJob*>( job );

  Akonadi::Item::List items;
  foreach ( Akonadi::Item item, origItems ) { //krazy:exclude=foreach non-const is intended here
    const DavItem davItem = davJob->item( item.remoteId() );

    // No data was retrieved for this item, maybe because it is not out of date
    if ( davItem.data().isEmpty() ) {
      items << item;
      continue;
    }

    kDebug() << "Multiget'ed item at " << davItem.url() << item.remoteId();

    // convert dav data into payload
    const QString data = QString::fromUtf8( davItem.data() );

    if ( item.mimeType() == KABC::Addressee::mimeType() ) {
      KABC::VCardConverter converter;
      const KABC::Addressee contact = converter.parseVCard( davItem.data() );

      if ( contact.isEmpty() )
        continue;

      item.setPayload<KABC::Addressee>( contact );
    } else {
      KCalCore::ICalFormat formatter;
      const IncidencePtr ptr( formatter.fromString( data ) );

      if ( !ptr )
        continue;

      item.setPayload<IncidencePtr>( ptr );

      // fix mime type for CalDAV collections
      item.setMimeType( ptr->mimeType() );
    }

    // update etag
    item.setRemoteRevision( davItem.etag() );
    mEtagCache.setEtag( item.remoteId(), davItem.etag() );
    items << item;
  }

  itemsRetrieved( items );
}

void DavGroupwareResource::onRetrieveItemFinished( KJob *job )
{
  onItemFetched( job, false );
}

void DavGroupwareResource::onItemRefreshed( KJob* job )
{
  onItemFetched( job, true );
}

void DavGroupwareResource::onItemFetched( KJob* job, bool isRefresh )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to retrieve item: %1", job->errorText() ) );
    return;
  }

  const DavItemFetchJob *fetchJob = qobject_cast<DavItemFetchJob*>( job );

  const DavItem davItem = fetchJob->item();

  Akonadi::Item item = fetchJob->property( "item" ).value<Akonadi::Item>();

  // convert dav data into payload
  const QString data = QString::fromUtf8( davItem.data() );

  if ( item.mimeType() == KABC::Addressee::mimeType() ) {
    KABC::VCardConverter converter;
    const KABC::Addressee contact = converter.parseVCard( davItem.data() );

    if ( contact.isEmpty() ) {
      cancelTask( i18n( "The server returned invalid data" ) );
      return;
    }

    item.setPayload<KABC::Addressee>( contact );
  } else {
    KCalCore::ICalFormat formatter;
    const IncidencePtr ptr( formatter.fromString( data ) );

    if ( !ptr ) {
      cancelTask( i18n( "The server returned invalid data" ) );
      return;
    }

    item.setPayload<IncidencePtr>( ptr );

    // fix mime type for CalDAV collections
    item.setMimeType( ptr->mimeType() );
  }

  // update etag
  item.setRemoteRevision( davItem.etag() );
  mEtagCache.setEtag( item.remoteId(), davItem.etag() );

  if ( isRefresh )
    changeCommitted( item );
  else
    itemRetrieved( item );
}

void DavGroupwareResource::onItemAddedFinished( KJob *job )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to add item: %1", job->errorText() ) );
    return;
  }

  const DavItemCreateJob *createJob = qobject_cast<DavItemCreateJob*>( job );

  const DavItem davItem = createJob->item();

  Akonadi::Item item = createJob->property( "item" ).value<Akonadi::Item>();
  item.setRemoteId( davItem.url() );

  if ( davItem.etag().isEmpty() ) {
    const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( item.parentCollection().remoteId(), item.remoteId() );
    DavItemFetchJob *fetchJob = new DavItemFetchJob( davUrl, davItem );
    fetchJob->setProperty( "item", QVariant::fromValue( item ) );
    connect( fetchJob, SIGNAL( result( KJob* ) ), SLOT( onItemRefreshed( KJob* ) ) );
    fetchJob->start();
  } else {
    item.setRemoteRevision( davItem.etag() );
    mEtagCache.setEtag( davItem.url(), davItem.etag() );
    changeCommitted( item );
  }
}

void DavGroupwareResource::onItemChangedFinished( KJob *job )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to change item: %1", job->errorText() ) );
    return;
  }

  const DavItemModifyJob *modifyJob = qobject_cast<DavItemModifyJob*>( job );

  const DavItem davItem = modifyJob->item();

  Akonadi::Item item = modifyJob->property( "item" ).value<Akonadi::Item>();

  if ( davItem.etag().isEmpty() ) {
    const DavUtils::DavUrl davUrl = Settings::self()->davUrlFromCollectionUrl( item.parentCollection().remoteId(), item.remoteId() );
    DavItemFetchJob *fetchJob = new DavItemFetchJob( davUrl, davItem );
    fetchJob->setProperty( "item", QVariant::fromValue( item ) );
    connect( fetchJob, SIGNAL( result( KJob* ) ), SLOT( onItemRefreshed( KJob* ) ) );
    fetchJob->start();
  } else {
    item.setRemoteRevision( davItem.etag() );
    mEtagCache.setEtag( davItem.url(), davItem.etag() );
    changeCommitted( item );
  }
}

void DavGroupwareResource::onItemRemovedFinished( KJob *job )
{
  if ( job->error() ) {
    cancelTask( i18n( "Unable to remove item: %1", job->errorText() ) );
    return;
  }

  changeProcessed();
}

void DavGroupwareResource::onCollectionDiscovered( const QString &collection, const QString &config )
{
  Settings::self()->addCollectionUrlMapping( collection, config );
}

bool DavGroupwareResource::configurationIsValid()
{
  if ( Settings::self()->remoteUrls().empty() )
    return false;

  int newICT = Settings::self()->refreshInterval();
  if ( newICT == 0 )
    newICT = -1;

  if ( newICT != mDavCollectionRoot.cachePolicy().intervalCheckTime() ) {
    Akonadi::CachePolicy cachePolicy = mDavCollectionRoot.cachePolicy();
    cachePolicy.setIntervalCheckTime( newICT );

    mDavCollectionRoot.setCachePolicy( cachePolicy );
  }

  if ( !Settings::self()->displayName().isEmpty() ) {
    EntityDisplayAttribute *attribute = mDavCollectionRoot.attribute<EntityDisplayAttribute>( Collection::AddIfMissing );
    attribute->setDisplayName( Settings::self()->displayName() );
    setName( Settings::self()->displayName() );
  }

  return true;
}

/*static*/
void DavGroupwareResource::setCollectionIcon( Akonadi::Collection &collection )
{
  const QStringList mimeTypes = collection.contentMimeTypes();
  if ( mimeTypes.count() == 1 ) {
    QHash<QString,QString> mapping;
    mapping.insert( KCalCore::Event::eventMimeType(), QLatin1String( "view-calendar" ) );
    mapping.insert( KCalCore::Todo::todoMimeType(), QLatin1String( "view-calendar-tasks" ) );
    mapping.insert( KCalCore::Journal::journalMimeType(), QLatin1String( "view-pim-journal" ) );
    mapping.insert( KABC::Addressee::mimeType(), QLatin1String( "view-pim-contacts" ) );

    if ( mapping.contains( mimeTypes.first() ) ) {
      EntityDisplayAttribute *attribute = collection.attribute<EntityDisplayAttribute>( Collection::AddIfMissing );
      attribute->setIconName( mapping.value( mimeTypes.first() ) );
    }
  }
}

AKONADI_RESOURCE_MAIN( DavGroupwareResource )

#include "davgroupwareresource.moc"
