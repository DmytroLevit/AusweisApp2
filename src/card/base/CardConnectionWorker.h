/*!
 * \brief Worker for \ref CardConnection that will do the job in \ref ReaderManagerWorker
 *
 * \copyright Copyright (c) 2015 Governikus GmbH & Co. KG
 */

#pragma once

#include "Apdu.h"
#include "CardReturnCode.h"
#include "Commands.h"
#include "EstablishPACEChannel.h"
#include "FileRef.h"
#include "Reader.h"
#include "SmartCardDefinitions.h"
#include "asn1/SecurityInfos.h"
#include "pace/SecureMessaging.h"

#include <QByteArray>

namespace governikus
{

/*!
 * This class represents a connection to a smart card.
 */
class CardConnectionWorker
	: public QObject
	, public QEnableSharedFromThis<CardConnectionWorker>
{
	private:
		Q_OBJECT

		/*!
		 * The connection talks to the Card held by the Reader
		 */
		QPointer<Reader> mReader;

		/*!
		 * Object performing the cryptography needed by a secure messaging channel
		 */
		QScopedPointer<SecureMessaging> mSecureMessaging;

		bool hasCard() const;
		inline QSharedPointer<const EFCardAccess> getEfCardAccess() const;

	private Q_SLOTS:
		void onReaderInfoChanged(const QString& pReaderName);

	protected:
		/*!
		 * The Card hold by the Reader is expected to be connected.
		 * The connection is closed, when the CardConnection is destroyed.
		 */
		CardConnectionWorker(Reader* pReader);

		/*!
		 * Destroys the CardConnection and disconnects from the card.
		 */
		virtual ~CardConnectionWorker();

	public:
		static QSharedPointer<CardConnectionWorker> create(Reader* pReader);

		Q_INVOKABLE ReaderInfo getReaderInfo() const;

		virtual CardReturnCode updateRetryCounter();

		virtual CardReturnCode readFile(const FileRef& pFileRef, QByteArray& pFileContent);

		virtual CardReturnCode transmit(const CommandApdu& pCommandApdu, ResponseApdu& pResponseApdu);

		/*!
		 * Performs PACE and establishes a PACE channel.
		 * If the Reader is a basic reader and the PACE channel is successfully established, the subsequent transmits will be secured using, secure messaging.
		 * I. e., a secure messaging channel is established.
		 */
		virtual CardReturnCode establishPaceChannel(PACE_PIN_ID pPinId,
				const QString& pPinValue,
				EstablishPACEChannelOutput& pChannelOutput);

		/*!
		 * Performs PACE and establishes a PACE channel for later terminal authentication.
		 * If the Reader is a basic reader and the PACE channel is successfully established, the subsequent transmits will be secured using, secure messaging.
		 * I. e., a secure messaging channel is established.
		 */
		virtual CardReturnCode establishPaceChannel(PACE_PIN_ID pPinId,
				const QString& pPinValue,
				const QByteArray& pChat,
				const QByteArray& pCertificateDescription,
				EstablishPACEChannelOutput& pChannelOutput);

		/*!
		 * Destroys a previously established PACE channel.
		 */
		virtual CardReturnCode destroyPaceChannel();

		/*!
		 * Destroys an established secure messaging channel, if there is one.
		 */
		virtual bool stopSecureMessaging();

		virtual CardReturnCode setEidPin(const QString& pNewPin, quint8 pTimeoutSeconds);

	Q_SIGNALS:
		void fireRetryCounterPotentiallyChanged();
		void fireReaderInfoChanged(const ReaderInfo& pReaderInfo);
};

} /* namespace governikus */
