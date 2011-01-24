/*
 * This file is part of TelepathyQt4
 *
 * Copyright (C) 2008-2010 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <TelepathyQt4/ContactManager>
#include "TelepathyQt4/contact-manager-roster-internal.h"

#include "TelepathyQt4/_gen/contact-manager.moc.hpp"

#include "TelepathyQt4/debug-internal.h"

#include <TelepathyQt4/AvatarData>
#include <TelepathyQt4/Connection>
#include <TelepathyQt4/ConnectionLowlevel>
#include <TelepathyQt4/ContactFactory>
#include <TelepathyQt4/PendingChannel>
#include <TelepathyQt4/PendingContactAttributes>
#include <TelepathyQt4/PendingContacts>
#include <TelepathyQt4/PendingFailure>
#include <TelepathyQt4/PendingHandles>
#include <TelepathyQt4/PendingVariantMap>
#include <TelepathyQt4/ReferencedHandles>
#include <TelepathyQt4/Utils>

#include <QMap>
#include <QWeakPointer>

namespace Tp
{

struct TELEPATHY_QT4_NO_EXPORT ContactManager::Private
{
    Private(ContactManager *parent, Connection *connection);
    ~Private();

    void ensureTracking(const Feature &feature);

    // avatar specific methods
    bool buildAvatarFileName(QString token, bool createDir,
        QString &avatarFileName, QString &mimeTypeFileName);

    ContactManager *parent;
    QWeakPointer<Connection> connection;
    ContactManager::Roster *roster;

    QMap<uint, QWeakPointer<Contact> > contacts;

    QMap<Feature, bool> tracking;
    Features supportedFeatures;

    // avatar
    UIntList requestAvatarsQueue;
    bool requestAvatarsIdle;
};

ContactManager::Private::Private(ContactManager *parent, Connection *connection)
    : parent(parent),
      connection(connection),
      roster(new ContactManager::Roster(parent))
{
}

ContactManager::Private::~Private()
{
    delete roster;
}

void ContactManager::Private::ensureTracking(const Feature &feature)
{
    if (tracking[feature]) {
        return;
    }

    ConnectionPtr conn(parent->connection());

    if (feature == Contact::FeatureAlias) {
        Client::ConnectionInterfaceAliasingInterface *aliasingInterface =
            conn->interface<Client::ConnectionInterfaceAliasingInterface>();

        parent->connect(
                aliasingInterface,
                SIGNAL(AliasesChanged(Tp::AliasPairList)),
                SLOT(onAliasesChanged(Tp::AliasPairList)));
    } else if (feature == Contact::FeatureAvatarData) {
        Client::ConnectionInterfaceAvatarsInterface *avatarsInterface =
            conn->interface<Client::ConnectionInterfaceAvatarsInterface>();

        parent->connect(
                avatarsInterface,
                SIGNAL(AvatarRetrieved(uint,QString,QByteArray,QString)),
                SLOT(onAvatarRetrieved(uint,QString,QByteArray,QString)));
    } else if (feature == Contact::FeatureAvatarToken) {
        Client::ConnectionInterfaceAvatarsInterface *avatarsInterface =
            conn->interface<Client::ConnectionInterfaceAvatarsInterface>();

        parent->connect(
                avatarsInterface,
                SIGNAL(AvatarUpdated(uint,QString)),
                SLOT(onAvatarUpdated(uint,QString)));
    } else if (feature == Contact::FeatureCapabilities) {
        Client::ConnectionInterfaceContactCapabilitiesInterface *contactCapabilitiesInterface =
            conn->interface<Client::ConnectionInterfaceContactCapabilitiesInterface>();

        parent->connect(
                contactCapabilitiesInterface,
                SIGNAL(ContactCapabilitiesChanged(Tp::ContactCapabilitiesMap)),
                SLOT(onCapabilitiesChanged(Tp::ContactCapabilitiesMap)));
    } else if (feature == Contact::FeatureInfo) {
        Client::ConnectionInterfaceContactInfoInterface *contactInfoInterface =
            conn->interface<Client::ConnectionInterfaceContactInfoInterface>();

        parent->connect(
                contactInfoInterface,
                SIGNAL(ContactInfoChanged(uint,Tp::ContactInfoFieldList)),
                SLOT(onContactInfoChanged(uint,Tp::ContactInfoFieldList)));
    } else if (feature == Contact::FeatureLocation) {
        Client::ConnectionInterfaceLocationInterface *locationInterface =
            conn->interface<Client::ConnectionInterfaceLocationInterface>();

        parent->connect(
                locationInterface,
                SIGNAL(LocationUpdated(uint,QVariantMap)),
                SLOT(onLocationUpdated(uint,QVariantMap)));
    } else if (feature == Contact::FeatureSimplePresence) {
        Client::ConnectionInterfaceSimplePresenceInterface *simplePresenceInterface =
            conn->interface<Client::ConnectionInterfaceSimplePresenceInterface>();

        parent->connect(
                simplePresenceInterface,
                SIGNAL(PresencesChanged(Tp::SimpleContactPresences)),
                SLOT(onPresencesChanged(Tp::SimpleContactPresences)));
    } else if (feature == Contact::FeatureRosterGroups) {
        // nothing to do here, but we don't want to warn
        ;
    } else {
        warning() << " Unknown feature" << feature
            << "when trying to figure out how to connect change notification!";
    }

    tracking[feature] = true;
}

bool ContactManager::Private::buildAvatarFileName(QString token, bool createDir,
        QString &avatarFileName, QString &mimeTypeFileName)
{
    QString cacheDir = QString(QLatin1String(qgetenv("XDG_CACHE_HOME")));
    if (cacheDir.isEmpty()) {
        cacheDir = QString(QLatin1String("%1/.cache")).arg(QLatin1String(qgetenv("HOME")));
    }

    ConnectionPtr conn(parent->connection());
    QString path = QString(QLatin1String("%1/telepathy/avatars/%2/%3")).
        arg(cacheDir).arg(conn->cmName()).arg(conn->protocolName());

    if (createDir && !QDir().mkpath(path)) {
        return false;
    }

    avatarFileName = QString(QLatin1String("%1/%2")).arg(path).arg(escapeAsIdentifier(token));
    mimeTypeFileName = QString(QLatin1String("%1.mime")).arg(avatarFileName);

    return true;
}

/**
 * \class ContactManager
 * \ingroup clientconn
 * \headerfile TelepathyQt4/contact-manager.h <TelepathyQt4/ContactManager>
 *
 * \brief The ContactManager class is responsible for managing contacts.
 */

/**
 * Construct a new ContactManager object.
 */
ContactManager::ContactManager(Connection *connection)
    : Object(),
      mPriv(new Private(this, connection))
{
}

/**
 * Class destructor.
 */
ContactManager::~ContactManager()
{
    delete mPriv;
}

/**
 * Return the connection owning this ContactManager.
 *
 * \return The connection owning this ContactManager.
 */
ConnectionPtr ContactManager::connection() const
{
    return ConnectionPtr(mPriv->connection);
}

Features ContactManager::supportedFeatures() const
{
    if (mPriv->supportedFeatures.isEmpty() &&
        connection()->interfaces().contains(QLatin1String(TELEPATHY_INTERFACE_CONNECTION_INTERFACE_CONTACTS))) {
        Features allFeatures = Features()
            << Contact::FeatureAlias
            << Contact::FeatureAvatarToken
            << Contact::FeatureAvatarData
            << Contact::FeatureSimplePresence
            << Contact::FeatureCapabilities
            << Contact::FeatureLocation
            << Contact::FeatureInfo;
        QStringList interfaces = connection()->lowlevel()->contactAttributeInterfaces();
        foreach (const Feature &feature, allFeatures) {
            if (interfaces.contains(featureToInterface(feature))) {
                mPriv->supportedFeatures.insert(feature);
            }
        }

        debug() << mPriv->supportedFeatures.size() << "contact features supported using" << this;
    }

    return mPriv->supportedFeatures;
}

/**
 * Return a list of relevant contacts (a reasonable guess as to what should
 * be displayed as "the contact list").
 *
 * This may include any or all of: contacts whose presence the user receives,
 * contacts who are allowed to see the user's presence, contacts stored in
 * some persistent contact list on the server, contacts who the user
 * has blocked from communicating with them, or contacts who are relevant
 * in some other way.
 *
 * User interfaces displaying a contact list will probably want to filter this
 * list and display some suitable subset of it.
 *
 * On protocols where there is no concept of presence or a centrally-stored
 * contact list (like IRC), this method may return an empty list.
 *
 * \return Some contacts
 */
Contacts ContactManager::allKnownContacts() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return Contacts();
    }

    return mPriv->roster->allKnownContacts();
}

/**
 * Return a list of user-defined contact list groups' names.
 *
 * This method requires Connection::FeatureRosterGroups to be enabled.
 *
 * \return List of user-defined contact list groups names.
 */
QStringList ContactManager::allKnownGroups() const
{
    if (!connection()->isReady(Connection::FeatureRosterGroups)) {
        return QStringList();
    }

    return mPriv->roster->allKnownGroups();
}

/**
 * Attempt to add an user-defined contact list group named \a group.
 *
 * This method requires Connection::FeatureRosterGroups to be enabled.
 *
 * On some protocols (e.g. XMPP) empty groups are not represented on the server,
 * so disconnecting from the server and reconnecting might cause empty groups to
 * vanish.
 *
 * The returned pending operation will finish successfully if the group already
 * exists.
 *
 * FIXME: currently, the returned pending operation will finish as soon as the
 * CM EnsureChannel has returned. At this point however the NewChannels
 * mechanism hasn't yet populated our contactListGroupChannels member, which
 * means one has to wait for groupAdded before being able to actually do
 * something with the group (which is error-prone!). This is fd.o #29728.
 *
 * \param group Group name.
 * \return A pending operation which will return when an attempt has been made
 *         to add an user-defined contact list group.
 * \sa groupAdded(), addContactsToGroup()
 */
PendingOperation *ContactManager::addGroup(const QString &group)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRosterGroups)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRosterGroups is not ready"),
                connection());
    }

    return mPriv->roster->addGroup(group);
}

/**
 * Attempt to remove an user-defined contact list group named \a group.
 *
 * This method requires Connection::FeatureRosterGroups to be enabled.
 *
 * FIXME: currently, the returned pending operation will finish as soon as the
 * CM close() has returned. At this point however the invalidated()
 * mechanism hasn't yet removed the channel from our contactListGroupChannels
 * member, which means contacts can seemingly still be added to the group etc.
 * until the change is picked up (and groupRemoved is emitted). This is fd.o
 * #29728.
 *
 * \param group Group name.
 * \return A pending operation which will return when an attempt has been made
 *         to remove an user-defined contact list group.
 * \sa groupRemoved(), removeContactsFromGroup()
 */
PendingOperation *ContactManager::removeGroup(const QString &group)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRosterGroups)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRosterGroups is not ready"),
                connection());
    }

    return mPriv->roster->removeGroup(group);
}

/**
 * Return the contacts in the given user-defined contact list group
 * named \a group.
 *
 * This method requires Connection::FeatureRosterGroups to be enabled.
 *
 * \param group Group name.
 * \return List of contacts on a user-defined contact list group, or an empty
 *         list if the group does not exist.
 * \sa allKnownGroups(), contactGroups()
 */
Contacts ContactManager::groupContacts(const QString &group) const
{
    if (!connection()->isReady(Connection::FeatureRosterGroups)) {
        return Contacts();
    }

    return mPriv->roster->groupContacts(group);
}

/**
 * Attempt to add the given \a contacts to the user-defined contact list
 * group named \a group.
 *
 * This method requires Connection::FeatureRosterGroups to be enabled.
 *
 * \param group Group name.
 * \param contacts Contacts to add.
 * \return A pending operation which will return when an attempt has been made
 *         to add the contacts to the user-defined contact list group.
 */
PendingOperation *ContactManager::addContactsToGroup(const QString &group,
        const QList<ContactPtr> &contacts)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRosterGroups)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRosterGroups is not ready"),
                connection());
    }

    return mPriv->roster->addContactsToGroup(group, contacts);
}

/**
 * Attempt to remove the given \a contacts from the user-defined contact list
 * group named \a group.
 *
 * This method requires Connection::FeatureRosterGroups to be enabled.
 *
 * \param group Group name.
 * \param contacts Contacts to remove.
 * \return A pending operation which will return when an attempt has been made
 *         to remove the contacts from the user-defined contact list group.
 */
PendingOperation *ContactManager::removeContactsFromGroup(const QString &group,
        const QList<ContactPtr> &contacts)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRosterGroups)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRosterGroups is not ready"),
                connection());
    }

    return mPriv->roster->removeContactsFromGroup(group, contacts);
}

/**
 * Return whether subscribing to additional contacts' presence is supported
 * on this channel.
 *
 * In some protocols, the list of contacts whose presence can be seen is
 * fixed, so we can't subscribe to the presence of additional contacts.
 *
 * Notably, in link-local XMPP, you can see the presence of everyone on the
 * local network, and trying to add more subscriptions would be meaningless.
 *
 * \return Whether Contact::requestPresenceSubscription and
 *         requestPresenceSubscription are likely to succeed
 */
bool ContactManager::canRequestPresenceSubscription() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->canRequestPresenceSubscription();
}

/**
 * Return whether a message can be sent when subscribing to contacts'
 * presence.
 *
 * If no message will actually be sent, user interfaces should avoid prompting
 * the user for a message, and use an empty string for the message argument.
 *
 * \return Whether the message argument to
 *         Contact::requestPresenceSubscription and
 *         requestPresenceSubscription is actually used
 */
bool ContactManager::subscriptionRequestHasMessage() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->subscriptionRequestHasMessage();
}

/**
 * Attempt to subscribe to the presence of the given contacts.
 *
 * This operation is sometimes called "adding contacts to the buddy
 * list" or "requesting authorization".
 *
 * This method requires Connection::FeatureRoster to be ready.
 *
 * On most protocols, the contacts will need to give permission
 * before the user will be able to receive their presence: if so, they will
 * be in presence state Contact::PresenceStateAsk until they authorize
 * or deny the request.
 *
 * The returned PendingOperation will return successfully when a request to
 * subscribe to the contacts' presence has been submitted, or fail if this
 * cannot happen. In particular, it does not wait for the contacts to give
 * permission for the presence subscription.
 *
 * \param contacts Contacts whose presence is desired
 * \param message A message from the user which is either transmitted to the
 *                contacts, or ignored, depending on the protocol
 * \return A pending operation which will return when an attempt has been made
 *         to subscribe to the contacts' presence
 */
PendingOperation *ContactManager::requestPresenceSubscription(
        const QList<ContactPtr> &contacts, const QString &message)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRoster)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRoster is not ready"),
                connection());
    }

    return mPriv->roster->requestPresenceSubscription(contacts, message);
}

/**
 * Return whether the user can stop receiving the presence of a contact
 * whose presence they have subscribed to.
 *
 * \return Whether removePresenceSubscription and
 *         Contact::removePresenceSubscription are likely to succeed
 *         for contacts with subscription state Contact::PresenceStateYes
 */
bool ContactManager::canRemovePresenceSubscription() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->canRemovePresenceSubscription();
}

/**
 * Return whether a message can be sent when removing an existing subscription
 * to the presence of a contact.
 *
 * If no message will actually be sent, user interfaces should avoid prompting
 * the user for a message, and use an empty string for the message argument.
 *
 * \return Whether the message argument to
 *         Contact::removePresenceSubscription and
 *         removePresenceSubscription is actually used,
 *         for contacts with subscription state Contact::PresenceStateYes
 */
bool ContactManager::subscriptionRemovalHasMessage() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->subscriptionRemovalHasMessage();
}

/**
 * Return whether the user can cancel a request to subscribe to a contact's
 * presence before that contact has responded.
 *
 * \return Whether removePresenceSubscription and
 *         Contact::removePresenceSubscription are likely to succeed
 *         for contacts with subscription state Contact::PresenceStateAsk
 */
bool ContactManager::canRescindPresenceSubscriptionRequest() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->canRescindPresenceSubscriptionRequest();
}

/**
 * Return whether a message can be sent when cancelling a request to
 * subscribe to the presence of a contact.
 *
 * If no message will actually be sent, user interfaces should avoid prompting
 * the user for a message, and use an empty string for the message argument.
 *
 * \return Whether the message argument to
 *         Contact::removePresenceSubscription and
 *         removePresenceSubscription is actually used,
 *         for contacts with subscription state Contact::PresenceStateAsk
 */
bool ContactManager::subscriptionRescindingHasMessage() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->subscriptionRescindingHasMessage();
}

/**
 * Attempt to stop receiving the presence of the given contacts, or cancel
 * a request to subscribe to their presence that was previously sent.
 *
 * This method requires Connection::FeatureRoster to be ready.
 *
 * \param contacts Contacts whose presence is no longer required
 * \message A message from the user which is either transmitted to the
 *          contacts, or ignored, depending on the protocol
 * \return A pending operation which will return when an attempt has been made
 *         to remove any subscription to the contacts' presence
 */
PendingOperation *ContactManager::removePresenceSubscription(
        const QList<ContactPtr> &contacts, const QString &message)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRoster)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRoster is not ready"),
                connection());
    }

    return mPriv->roster->removePresenceSubscription(contacts, message);
}

/**
 * Return true if the publication of the user's presence to contacts can be
 * authorized.
 *
 * This is always true, unless the protocol has no concept of authorizing
 * publication (in which case contacts' publication status can never be
 * Contact::PresenceStateAsk).
 */
bool ContactManager::canAuthorizePresencePublication() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->canAuthorizePresencePublication();
}

/**
 * Return whether a message can be sent when authorizing a request from a
 * contact that the user's presence is published to them.
 *
 * If no message will actually be sent, user interfaces should avoid prompting
 * the user for a message, and use an empty string for the message argument.
 *
 * \return Whether the message argument to
 *         Contact::authorizePresencePublication and
 *         authorizePresencePublication is actually used,
 *         for contacts with subscription state Contact::PresenceStateAsk
 */
bool ContactManager::publicationAuthorizationHasMessage() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->publicationAuthorizationHasMessage();
}

/**
 * If the given contacts have asked the user to publish presence to them,
 * grant permission for this publication to take place.
 *
 * This method requires Connection::FeatureRoster to be ready.
 *
 * \param contacts Contacts who should be allowed to receive the user's
 *                 presence
 * \message A message from the user which is either transmitted to the
 *          contacts, or ignored, depending on the protocol
 * \return A pending operation which will return when an attempt has been made
 *         to authorize publication of the user's presence to the contacts
 */
PendingOperation *ContactManager::authorizePresencePublication(
        const QList<ContactPtr> &contacts, const QString &message)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRoster)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRoster is not ready"),
                connection());
    }

    return mPriv->roster->authorizePresencePublication(contacts, message);
}

/**
 * Return whether a message can be sent when rejecting a request from a
 * contact that the user's presence is published to them.
 *
 * If no message will actually be sent, user interfaces should avoid prompting
 * the user for a message, and use an empty string for the message argument.
 *
 * \return Whether the message argument to
 *         Contact::removePresencePublication and
 *         removePresencePublication is actually used,
 *         for contacts with subscription state Contact::PresenceStateAsk
 */
bool ContactManager::publicationRejectionHasMessage() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->publicationRejectionHasMessage();
}

/**
 * Return true if the publication of the user's presence to contacts can be
 * removed, even after permission has been given.
 *
 * (Rejecting requests for presence to be published is always allowed.)
 *
 * \return Whether removePresencePublication and
 *         Contact::removePresencePublication are likely to succeed
 *         for contacts with subscription state Contact::PresenceStateYes
 */
bool ContactManager::canRemovePresencePublication() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->canRemovePresencePublication();
}

/**
 * Return whether a message can be sent when revoking earlier permission
 * that the user's presence is published to a contact.
 *
 * If no message will actually be sent, user interfaces should avoid prompting
 * the user for a message, and use an empty string for the message argument.
 *
 * \return Whether the message argument to
 *         Contact::removePresencePublication and
 *         removePresencePublication is actually used,
 *         for contacts with subscription state Contact::PresenceStateYes
 */
bool ContactManager::publicationRemovalHasMessage() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->publicationRemovalHasMessage();
}

/**
 * If the given contacts have asked the user to publish presence to them,
 * deny this request (this should always succeed, unless a network error
 * occurs).
 *
 * This method requires Connection::FeatureRoster to be ready.
 *
 * If the given contacts already have permission to receive
 * the user's presence, attempt to revoke that permission (this might not
 * be supported by the protocol - canRemovePresencePublication
 * indicates whether it is likely to succeed).
 *
 * \param contacts Contacts who should no longer be allowed to receive the
 *                 user's presence
 * \message A message from the user which is either transmitted to the
 *          contacts, or ignored, depending on the protocol
 * \return A pending operation which will return when an attempt has been made
 *         to remove any publication of the user's presence to the contacts
 */
PendingOperation *ContactManager::removePresencePublication(
        const QList<ContactPtr> &contacts, const QString &message)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRoster)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRoster is not ready"),
                connection());
    }

    return mPriv->roster->removePresencePublication(contacts, message);
}

/**
 * Remove completely contacts from the server. It has the same effect than
 * calling removePresencePublication() and removePresenceSubscription(),
 * but also remove from 'stored' list if it exists.
 *
 * \param contacts Contacts who should be removed
 * \message A message from the user which is either transmitted to the
 *          contacts, or ignored, depending on the protocol
 * \return A pending operation which will return when an attempt has been made
 *         to remove any publication of the user's presence to the contacts
 */
PendingOperation *ContactManager::removeContacts(
        const QList<ContactPtr> &contacts, const QString &message)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRoster)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRoster is not ready"),
                connection());
    }

    return mPriv->roster->removeContacts(contacts, message);
}

/**
 * Return whether this protocol has a list of blocked contacts.
 *
 * \return Whether blockContacts is likely to succeed
 */
bool ContactManager::canBlockContacts() const
{
    if (!connection()->isReady(Connection::FeatureRoster)) {
        return false;
    }

    return mPriv->roster->canBlockContacts();
}

/**
 * Set whether the given contacts are blocked. Blocked contacts cannot send
 * messages to the user; depending on the protocol, blocking a contact may
 * have other effects.
 *
 * This method requires Connection::FeatureRoster to be ready.
 *
 * \param contacts Contacts who should be added to, or removed from, the list
 *                 of blocked contacts
 * \param value If true, add the contacts to the list of blocked contacts;
 *              if false, remove them from the list
 * \return A pending operation which will return when an attempt has been made
 *         to take the requested action
 */
PendingOperation *ContactManager::blockContacts(
        const QList<ContactPtr> &contacts, bool value)
{
    if (!connection()->isValid()) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"),
                connection());
    } else if (!connection()->isReady(Connection::FeatureRoster)) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureRoster is not ready"),
                connection());
    }

    return mPriv->roster->blockContacts(contacts, value);
}

PendingContacts *ContactManager::contactsForHandles(const UIntList &handles,
        const Features &features)
{
    QMap<uint, ContactPtr> satisfyingContacts;
    QSet<uint> otherContacts;
    Features missingFeatures;

    Features realFeatures(features);
    realFeatures.unite(connection()->contactFactory()->features());

    if (!connection()->isValid()) {
        return new PendingContacts(ContactManagerPtr(this), handles, realFeatures, QStringList(),
                satisfyingContacts, otherContacts, QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"));
    } else if (!connection()->isReady(Connection::FeatureCore)) {
        return new PendingContacts(ContactManagerPtr(this), handles, realFeatures, QStringList(),
                satisfyingContacts, otherContacts, QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureCore is not ready"));
    }

    foreach (uint handle, handles) {
        ContactPtr contact = lookupContactByHandle(handle);
        if (contact) {
            if ((realFeatures - contact->requestedFeatures()).isEmpty()) {
                // Contact exists and has all the requested features
                satisfyingContacts.insert(handle, contact);
            } else {
                // Contact exists but is missing features
                otherContacts.insert(handle);
                missingFeatures.unite(realFeatures - contact->requestedFeatures());
            }
        } else {
            // Contact doesn't exist - we need to get all of the features (same as unite(features))
            missingFeatures = realFeatures;
            otherContacts.insert(handle);
        }
    }

    Features supported = supportedFeatures();
    QSet<QString> interfaces;
    foreach (const Feature &feature, missingFeatures) {
        mPriv->ensureTracking(feature);

        if (supported.contains(feature)) {
            // Only query interfaces which are reported as supported to not get an error
            interfaces.insert(featureToInterface(feature));
        }
    }

    PendingContacts *contacts =
        new PendingContacts(ContactManagerPtr(this), handles, realFeatures, interfaces.toList(),
                satisfyingContacts, otherContacts);
    return contacts;
}

PendingContacts *ContactManager::contactsForHandles(const ReferencedHandles &handles,
        const Features &features)
{
    return contactsForHandles(handles.toList(), features);
}

PendingContacts *ContactManager::contactsForIdentifiers(const QStringList &identifiers,
        const Features &features)
{
    if (!connection()->isValid()) {
        return new PendingContacts(ContactManagerPtr(this), identifiers, features,
                QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"));
    } else if (!connection()->isReady(Connection::FeatureCore)) {
        return new PendingContacts(ContactManagerPtr(this), identifiers, features,
                QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureCore is not ready"));
    }

    Features realFeatures(features);
    realFeatures.unite(connection()->contactFactory()->features());
    PendingContacts *contacts = new PendingContacts(ContactManagerPtr(this), identifiers,
            realFeatures);
    return contacts;
}

PendingContacts *ContactManager::upgradeContacts(const QList<ContactPtr> &contacts,
        const Features &features)
{
    if (!connection()->isValid()) {
        return new PendingContacts(ContactManagerPtr(this), contacts, features,
                QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection is invalid"));
    } else if (!connection()->isReady(Connection::FeatureCore)) {
        return new PendingContacts(ContactManagerPtr(this), contacts, features,
                QLatin1String(TELEPATHY_ERROR_NOT_AVAILABLE),
                QLatin1String("Connection::FeatureCore is not ready"));
    }

    return new PendingContacts(ContactManagerPtr(this), contacts, features);
}

ContactPtr ContactManager::lookupContactByHandle(uint handle)
{
    ContactPtr contact;

    if (mPriv->contacts.contains(handle)) {
        contact = ContactPtr(mPriv->contacts.value(handle));
        if (!contact) {
            // Dangling weak pointer, remove it
            mPriv->contacts.remove(handle);
        }
    }

    return contact;
}

void ContactManager::requestContactAvatar(Contact *contact)
{
    QString avatarFileName;
    QString mimeTypeFileName;

    bool success = (contact->isAvatarTokenKnown() &&
        mPriv->buildAvatarFileName(contact->avatarToken(), false,
            avatarFileName, mimeTypeFileName));

    /* Check if the avatar is already in the cache */
    if (success && QFile::exists(avatarFileName)) {
        QFile mimeTypeFile(mimeTypeFileName);
        mimeTypeFile.open(QIODevice::ReadOnly);
        QString mimeType = QString(QLatin1String(mimeTypeFile.readAll()));
        mimeTypeFile.close();

        debug() << "Avatar found in cache for handle" << contact->handle()[0];
        debug() << "Filename:" << avatarFileName;
        debug() << "MimeType:" << mimeType;

        contact->receiveAvatarData(AvatarData(avatarFileName, mimeType));

        return;
    }

    /* Not found in cache, queue this contact. We do this to group contacts
     * for the AvatarRequest call */
    debug() << "Need to request avatar for handle" << contact->handle()[0];
    if (!mPriv->requestAvatarsIdle) {
        QTimer::singleShot(0, this, SLOT(doRequestAvatars()));
        mPriv->requestAvatarsIdle = true;
    }
    mPriv->requestAvatarsQueue.append(contact->handle()[0]);
}

void ContactManager::onAliasesChanged(const AliasPairList &aliases)
{
    debug() << "Got AliasesChanged for" << aliases.size() << "contacts";

    foreach (AliasPair pair, aliases) {
        ContactPtr contact = lookupContactByHandle(pair.handle);

        if (contact) {
            contact->receiveAlias(pair.alias);
        }
    }
}

void ContactManager::doRequestAvatars()
{
    debug() << "Request" << mPriv->requestAvatarsQueue.size() << "avatar(s)";

    Client::ConnectionInterfaceAvatarsInterface *avatarsInterface =
        connection()->interface<Client::ConnectionInterfaceAvatarsInterface>();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
        avatarsInterface->RequestAvatars(mPriv->requestAvatarsQueue),
        this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)), watcher,
        SLOT(deleteLater()));

    mPriv->requestAvatarsQueue = UIntList();
    mPriv->requestAvatarsIdle = false;
}

void ContactManager::onAvatarUpdated(uint handle, const QString &token)
{
    debug() << "Got AvatarUpdate for contact with handle" << handle;

    ContactPtr contact = lookupContactByHandle(handle);
    if (contact) {
        contact->receiveAvatarToken(token);
    }
}

void ContactManager::onAvatarRetrieved(uint handle, const QString &token,
    const QByteArray &data, const QString &mimeType)
{
    QString avatarFileName;
    QString mimeTypeFileName;

    debug() << "Got AvatarRetrieved for contact with handle" << handle;

    bool success = mPriv->buildAvatarFileName(token, true, avatarFileName,
        mimeTypeFileName);

    if (success) {
        QFile mimeTypeFile(mimeTypeFileName);
        QFile avatarFile(avatarFileName);

        debug() << "Write avatar in cache for handle" << handle;
        debug() << "Filename:" << avatarFileName;
        debug() << "MimeType:" << mimeType;

        mimeTypeFile.open(QIODevice::WriteOnly);
        mimeTypeFile.write(mimeType.toLatin1());
        mimeTypeFile.close();

        avatarFile.open(QIODevice::WriteOnly);
        avatarFile.write(data);
        avatarFile.close();
    }

    ContactPtr contact = lookupContactByHandle(handle);
    if (contact) {
        contact->setAvatarToken(token);
        contact->receiveAvatarData(AvatarData(avatarFileName, mimeType));
    }
}

void ContactManager::onPresencesChanged(const SimpleContactPresences &presences)
{
    debug() << "Got PresencesChanged for" << presences.size() << "contacts";

    foreach (uint handle, presences.keys()) {
        ContactPtr contact = lookupContactByHandle(handle);

        if (contact) {
            contact->receiveSimplePresence(presences[handle]);
        }
    }
}

void ContactManager::onCapabilitiesChanged(const ContactCapabilitiesMap &caps)
{
    debug() << "Got ContactCapabilitiesChanged for" << caps.size() << "contacts";

    foreach (uint handle, caps.keys()) {
        ContactPtr contact = lookupContactByHandle(handle);

        if (contact) {
            contact->receiveCapabilities(caps[handle]);
        }
    }
}

void ContactManager::onLocationUpdated(uint handle, const QVariantMap &location)
{
    debug() << "Got LocationUpdated for contact with handle" << handle;

    ContactPtr contact = lookupContactByHandle(handle);

    if (contact) {
        contact->receiveLocation(location);
    }
}

void ContactManager::onContactInfoChanged(uint handle, const Tp::ContactInfoFieldList &info)
{
    debug() << "Got ContactInfoChanged for contact with handle" << handle;

    ContactPtr contact = lookupContactByHandle(handle);

    if (contact) {
        contact->receiveInfo(info);
    }
}

ContactPtr ContactManager::ensureContact(const ReferencedHandles &handle,
        const Features &features, const QVariantMap &attributes)
{
    uint bareHandle = handle[0];
    ContactPtr contact = lookupContactByHandle(bareHandle);

    if (!contact) {
        contact = connection()->contactFactory()->construct(this, handle, features, attributes);
        mPriv->contacts.insert(bareHandle, contact.data());
    } else {
        contact->augment(features, attributes);
    }

    return contact;
}

QString ContactManager::featureToInterface(const Feature &feature)
{
    if (feature == Contact::FeatureAlias) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_ALIASING;
    } else if (feature == Contact::FeatureAvatarToken) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_AVATARS;
    } else if (feature == Contact::FeatureAvatarData) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_AVATARS;
    } else if (feature == Contact::FeatureSimplePresence) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE;
    } else if (feature == Contact::FeatureCapabilities) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES;
    } else if (feature == Contact::FeatureLocation) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_LOCATION;
    } else if (feature == Contact::FeatureInfo) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_CONTACT_INFO;
    } else if (feature == Contact::FeatureRosterGroups) {
        return TP_QT4_IFACE_CONNECTION_INTERFACE_CONTACT_GROUPS;
    } else {
        warning() << "ContactManager doesn't know which interface corresponds to feature"
            << feature;
        return QString();
    }
}

PendingOperation *ContactManager::introspectRoster()
{
    return mPriv->roster->introspect();
}

PendingOperation *ContactManager::introspectRosterGroups()
{
    return mPriv->roster->introspectGroups();
}

void ContactManager::resetRoster()
{
    mPriv->roster->reset();
}

/**
 * \fn void ContactManager::presencePublicationRequested(const Tp::Contacts &contacts);
 *
 * This signal is emitted whenever some contacts request for presence publication.
 *
 * \param contacts A set of contacts which requested presence publication.
 */

/**
 * \fn void ContactManager::presencePublicationRequested(const Tp::Contacts &contacts,
 *          const QString &message);
 *
 * \deprecated Turned out this didn't make sense at all. There can be multiple contacts, but this
 *             signal carries just a single message.
 *             Use presencePublicationRequested(const Tp::Contacts &contacts) instead,
 *             and extract the messages from the individual Tp::Contact objects instead.
 */

/**
 * \fn void ContactManager::presencePublicationRequested(const Tp::Contacts &contacts,
 *          const Tp::Channel::GroupMemberChangeDetails &details);
 *
 * \deprecated Turned out this didn't make sense at all. There can be multiple contacts, but this
 *             signal carries just a single details.
 *             Use presencePublicationRequested(const Tp::Contacts &contacts) instead,
 *             and extract the details (message) from the individual Tp::Contact objects instead.
 */

/**
 * \fn void ContactManager::groupMembersChanged(const QString &group,
 *          const Tp::Contacts &groupMembersAdded,
 *          const Tp::Contacts &groupMembersRemoved,
 *          const Tp::Channel::GroupMemberChangeDetails &details);
 *
 * This signal is emitted whenever some contacts got removed or added from
 * a group.
 *
 * \param group The name of the group that changed.
 * \param groupMembersAdded A set of contacts which were added to the group \a group.
 * \param groupMembersRemoved A set of contacts which were removed from the group \a group.
 * \param details The change details.
 */

/**
 * \fn void ContactManager::allKnownContactsChanged(const Tp::Contacts &contactsAdded,
 *          const Tp::Contacts &contactsRemoved,
 *          const Tp::Channel::GroupMemberChangeDetails &details);
 *
 * This signal is emitted whenever some contacts got removed or added from
 * ContactManager's known contact list. It is useful for monitoring which contacts
 * are currently known by ContactManager.
 *
 * \param contactsAdded A set of contacts which were added to the known contact list.
 * \param contactsRemoved A set of contacts which were removed from the known contact list.
 * \param details The change details.
 *
 * \note Please note that, in some protocols, this signal could stream newly added contacts
 *       with both presence subscription and publication state set to No. Be sure to watch
 *       over publication and/or subscription state changes if that is the case.
 */

void ContactManager::connectNotify(const char *signalName)
{
    if (qstrcmp(signalName, SIGNAL(presencePublicationRequested(Tp::Contacts,Tp::Channel::GroupMemberChangeDetails))) == 0) {
        warning() << "Connecting to deprecated signal presencePublicationRequested(Tp::Contacts,Tp::Channel::GroupMemberChangeDetails)";
    } else if (qstrcmp(signalName, SIGNAL(presencePublicationRequested(Tp::Contacts,QString))) == 0) {
        warning() << "Connecting to deprecated signal presencePublicationRequested(Tp::Contacts,QString)";
    }
}

} // Tp
