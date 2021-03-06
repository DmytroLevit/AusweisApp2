/*!
 * PcscCard.cpp
 *
 * \copyright Copyright (c) 2014 Governikus GmbH & Co. KG
 */

#include "DestroyPACEChannel.h"
#include "PcscCard.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(card_pcsc)

using namespace governikus;

PcscCard::PcscCard(PcscReader* pPcscReader)
	: Card()
	, mReader(pPcscReader)
	, mProtocol(0)
	, mContextHandle(0)
	, mCardHandle(0)
	, mTimer()
{
	PCSC_RETURNCODE returnCode = SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &mContextHandle);
	qCDebug(card_pcsc) << "SCardEstablishContext for " << mReader->getName() << ": " << PcscUtils::toString(returnCode);

	mTimer.setInterval(4000);
	QObject::connect(&mTimer, &QTimer::timeout, this, &PcscCard::sendSCardStatus);
}


PcscCard::~PcscCard()
{
	qCDebug(card_pcsc) << mReader->getName();
	if (PcscCard::isConnected())
	{
		PcscCard::disconnect();
	}

	PCSC_RETURNCODE returnCode = SCardReleaseContext(mContextHandle);
	qCDebug(card_pcsc) << "SCardReleaseContext for " << mReader->getName() << ": " << PcscUtils::toString(returnCode);
	mContextHandle = 0;
}


void PcscCard::sendSCardStatus()
{
	/*
	 * According to the documentation of SCardBeginTransaction:
	 *
	 * "If a transaction is held on the card for more than five seconds with no operations happening on that card,
	 * then the card is reset.
	 * Calling any of the Smart Card and Reader Access Functions or Direct Card Access Functions on the card that
	 * is transacted results in the timer being reset to continue allowing the transaction to be used."
	 *
	 * When sending a transmit request after resetting, we get a SCARD_W_RESET_CARD back. That breaks any active
	 * secure messaging channels and results in cancelled authentications.
	 *
	 * To work around that issue, we send a SCardStatus as ping every four seconds to prevent the timeout.
	 */
	SCardStatus(mCardHandle, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}


CardReturnCode PcscCard::connect()
{
	if (PcscCard::isConnected())
	{
		qCCritical(card_pcsc) << "Card is already connected";
		return CardReturnCode::COMMAND_FAILED;
	}

	PCSC_INT shareMode = SCARD_SHARE_SHARED;
	PCSC_INT preferredProtocols = SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1;

	PCSC_RETURNCODE returnCode = SCardConnect(mContextHandle, mReader->getState().szReader, shareMode, preferredProtocols, &mCardHandle, &mProtocol);
	qCDebug(card_pcsc) << "SCardConnect for " << mReader->getName() << " : " << PcscUtils::toString(returnCode) << " cardHandle: " << mCardHandle << " protocol: T" << (mProtocol - 1);
	if (returnCode != PcscUtils::Scard_S_Success)
	{
		return CardReturnCode::COMMAND_FAILED;
	}

	returnCode = SCardBeginTransaction(mCardHandle);
	qCDebug(card_pcsc) << "SCardBeginTransaction for " << mReader->getName() << " : " << PcscUtils::toString(returnCode);
	if (returnCode != PcscUtils::Scard_S_Success)
	{
		SCardDisconnect(mCardHandle, SCARD_LEAVE_CARD);
		return CardReturnCode::COMMAND_FAILED;
	}

	if (QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS8)
	{
		mTimer.start();
	}
	return CardReturnCode::OK;
}


CardReturnCode PcscCard::disconnect()
{
	if (!PcscCard::isConnected())
	{
		qCCritical(card_pcsc) << "Card is already disconnected";
		return CardReturnCode::COMMAND_FAILED;
	}
	mTimer.stop();

	PCSC_RETURNCODE returnCode = SCardEndTransaction(mCardHandle, SCARD_LEAVE_CARD);
	qCDebug(card_pcsc) << "SCardEndTransaction for " << mReader->getName() << " : " << PcscUtils::toString(returnCode);

	returnCode = SCardDisconnect(mCardHandle, SCARD_RESET_CARD);
	mCardHandle = 0;
	mProtocol = 0;
	qCDebug(card_pcsc) << "SCardDisconnect for " << mReader->getName() << " : " << PcscUtils::toString(returnCode);

	return returnCode == PcscUtils::Scard_S_Success ? CardReturnCode::OK : CardReturnCode::COMMAND_FAILED;
}


bool PcscCard::isConnected()
{
	return mCardHandle != 0;
}


CardReturnCode PcscCard::transmit(const CommandApdu& pCmd, ResponseApdu& pRes)
{
	if (!isConnected())
	{
		qCCritical(card_pcsc) << "Card is not connected, abort transmit";
		return CardReturnCode::COMMAND_FAILED;
	}

	QByteArray receiveBuffer;
	PCSC_RETURNCODE returnCode = transmit(pCmd.getBuffer(), receiveBuffer);
	if (returnCode != PcscUtils::Scard_S_Success)
	{
		return CardReturnCode::COMMAND_FAILED;
	}

	ResponseApdu tempResponse;
	tempResponse.setBuffer(receiveBuffer);

	if (tempResponse.getSW1() == 0x6c)
	{
		qCDebug(card_pcsc) << "got SW1 == 0x6c, retransmitting with new Le:" << tempResponse.getSW2();
		CommandApdu retransmitCommand(pCmd.getCLA(), pCmd.getINS(), pCmd.getP1(), pCmd.getP2(), pCmd.getData(), tempResponse.getSW2());
		returnCode = transmit(retransmitCommand.getBuffer(), receiveBuffer);
		if (returnCode != PcscUtils::Scard_S_Success)
		{
			return CardReturnCode::COMMAND_FAILED;
		}
	}

	while (tempResponse.getSW1() == 0x61)
	{
		QByteArray tempReceiveBuffer;
		qCDebug(card_pcsc) << "got SW1 == 0x61, getting response with Le:" << tempResponse.getSW2();
		CommandApdu getResponseCommand(0, char(0xC0), 0, 0, QByteArray(), tempResponse.getSW2());
		returnCode = transmit(getResponseCommand.getBuffer(), tempReceiveBuffer);
		if (returnCode != PcscUtils::Scard_S_Success)
		{
			return CardReturnCode::COMMAND_FAILED;
		}

		tempResponse.setBuffer(tempReceiveBuffer);
		// cut off sw1 and sw2
		receiveBuffer.resize(receiveBuffer.size() - 2);
		receiveBuffer += tempReceiveBuffer;
	}

	pRes.setBuffer(receiveBuffer);
	return CardReturnCode::OK;
}


PCSC_RETURNCODE PcscCard::transmit(const QByteArray& pSendBuffer, QByteArray& pReceiveBuffer)
{
	const SCARD_IO_REQUEST* sendPci;
	switch (mProtocol)
	{
		case SCARD_PROTOCOL_T0:
			sendPci = SCARD_PCI_T0;
			break;

		case SCARD_PROTOCOL_T1:
			sendPci = SCARD_PCI_T1;
			break;

		case SCARD_PROTOCOL_RAW:
			sendPci = SCARD_PCI_RAW;
			break;

		default:
			qCDebug(card_pcsc) << "unsupported protocol";
			return PcscUtils::Scard_E_Proto_Mismatch;
	}

	SCARD_IO_REQUEST recvPci;
	recvPci.dwProtocol = mProtocol;
	recvPci.cbPciLength = sizeof(SCARD_IO_REQUEST);

	pReceiveBuffer.fill(0x00, 8192);
	PCSC_INT bytesReceived = static_cast<PCSC_INT>(pReceiveBuffer.size());
	PCSC_RETURNCODE returnCode = transmit(pSendBuffer, pReceiveBuffer, sendPci, recvPci, bytesReceived);

	/*
	 * Reconnecting makes only sense, when no secure messaging channel is active.
	 * Otherwise the secure messaging channel is destroyed and the transmit will fail anyway.
	 */
	if (returnCode == PcscUtils::Scard_W_Reset_Card && !CommandApdu::isSecureMessaging(pSendBuffer))
	{
		returnCode = SCardReconnect(mCardHandle, SCARD_SHARE_SHARED, mProtocol, SCARD_RESET_CARD, nullptr);
		qCDebug(card_pcsc) << "Reconnect to Card";

		if (returnCode != PcscUtils::Scard_S_Success)
		{
			qCCritical(card_pcsc) << "SCardReconnect failed:" << PcscUtils::toString(returnCode);
			return returnCode;
		}

		returnCode = SCardBeginTransaction(mCardHandle);
		qCDebug(card_pcsc) << "SCardBeginTransaction:" << PcscUtils::toString(returnCode);
		bytesReceived = static_cast<PCSC_INT>(pReceiveBuffer.size());
		returnCode = transmit(pSendBuffer, pReceiveBuffer, sendPci, recvPci, bytesReceived);
	}

	if (returnCode != PcscUtils::Scard_S_Success)
	{
		return returnCode;
	}
	if (bytesReceived > INT_MAX)
	{
		qCCritical(card_pcsc) << "Max allowed receive buffer size exceeded";
		Q_ASSERT(bytesReceived <= INT_MAX);
		return PcscUtils::Scard_F_Unknown_Error;
	}
	pReceiveBuffer.resize(static_cast<int>(bytesReceived));
	qCDebug(card_pcsc) << "SCardTransmit resBuffer " << pReceiveBuffer.toHex();
	if (pReceiveBuffer.size() < 2)
	{
		qCCritical(card_pcsc) << "Response buffer smaller than 2";
		return PcscUtils::Scard_F_Unknown_Error;
	}
	return PcscUtils::Scard_S_Success;
}


PCSC_RETURNCODE PcscCard::transmit(const QByteArray& pSendBuffer,
		QByteArray& pReceiveBuffer,
		const SCARD_IO_REQUEST* pSendPci,
		SCARD_IO_REQUEST& pRecvPci,
		PCSC_INT& pBytesReceived)
{
	qCDebug(card_pcsc) << "SCardTransmit cmdBuffer " << pSendBuffer.toHex();
	PCSC_RETURNCODE returnCode = SCardTransmit(mCardHandle, pSendPci, reinterpret_cast<PCSC_CUCHAR_PTR>(pSendBuffer.data()), static_cast<PCSC_INT>(pSendBuffer.size()), &pRecvPci, reinterpret_cast<PCSC_UCHAR_PTR>(pReceiveBuffer.data()), &pBytesReceived);
	qCDebug(card_pcsc) << "SCardTransmit for " << mReader->getName() << " : " << PcscUtils::toString(returnCode);

	return returnCode;
}


CardReturnCode PcscCard::establishPaceChannel(PACE_PIN_ID pPinId,
		const QByteArray& pChat,
		const QByteArray& pCertificateDescription,
		EstablishPACEChannelOutput& pChannelOutput, quint8 pTimeoutSeconds)
{
	Q_UNUSED(pTimeoutSeconds);
	if (!mReader->hasFeature(FeatureID::EXECUTE_PACE))
	{
		return CardReturnCode::COMMAND_FAILED;
	}
	PCSC_INT cmdID = mReader->getFeatureValue(FeatureID::EXECUTE_PACE);

	EstablishPACEChannelBuilder builder;
	builder.setPinId(pPinId);
	builder.setChat(pChat);
	builder.setCertificateDescription(pCertificateDescription);

	QByteArray controlRes;
	PCSC_RETURNCODE returnCode = control(cmdID, builder.createCommandData(), controlRes);
	if (returnCode != PcscUtils::Scard_S_Success)
	{
		qCWarning(card_pcsc) << "Control to establish PACE channel failed";
		return CardReturnCode::COMMAND_FAILED;
	}

	pChannelOutput.parse(controlRes, pPinId);
	return pChannelOutput.getPaceReturnCode();
}


CardReturnCode PcscCard::destroyPaceChannel()
{
	if (!mReader->hasFeature(FeatureID::EXECUTE_PACE))
	{
		return CardReturnCode::COMMAND_FAILED;
	}
	PCSC_INT cmdID = mReader->getFeatureValue(FeatureID::EXECUTE_PACE);

	DestroyPACEChannelBuilder builder;
	QByteArray controlRes;
	PCSC_RETURNCODE returnCode = control(cmdID, builder.createCommandData(), controlRes);
	if (returnCode != PcscUtils::Scard_S_Success)
	{
		qCWarning(card_pcsc) << "Control to destroy PACE channel failed";
		return CardReturnCode::COMMAND_FAILED;
	}
	return CardReturnCode::OK;
}


PCSC_RETURNCODE PcscCard::control(PCSC_INT pCntrCode, const QByteArray& pCntrInput, QByteArray& pCntrOutput)
{
	char buffer[2048];
	PCSC_INT len = 0;
	qCDebug(card_pcsc) << "SCardControl cmdBuffer " << pCntrInput.toHex();
	PCSC_RETURNCODE returnCode = SCardControl(mCardHandle,
			pCntrCode,
			pCntrInput.constData(),
			static_cast<PCSC_INT>(pCntrInput.size()),
			buffer,
			sizeof(buffer),
			&len);

	if (returnCode != PcscUtils::Scard_S_Success)
	{
		len = 0;
	}

	if (sizeof(buffer) < len)
	{
		qCCritical(card_pcsc) << "Buffer size smaller than read length";
		Q_ASSERT(sizeof(buffer) >= len);
		return PcscUtils::Scard_F_Unknown_Error;
	}
	if (len > INT_MAX)
	{
		qCCritical(card_pcsc) << "Read length bigger than INT_MAX";
		Q_ASSERT(len <= INT_MAX);
		return PcscUtils::Scard_F_Unknown_Error;
	}
	pCntrOutput.append(buffer, static_cast<int>(len));

	qCDebug(card_pcsc) << "SCardControl for " << mReader->getName() << " : " << PcscUtils::toString(returnCode) << " " << pCntrOutput.toHex();
	return returnCode;
}


CardReturnCode PcscCard::setEidPin(uchar pTimeoutSeconds)
{
	if (!mReader->hasFeature(FeatureID::MODIFY_PIN_DIRECT))
	{
		return CardReturnCode::COMMAND_FAILED;
	}
	PCSC_INT cmdID = mReader->getFeatureValue(FeatureID::MODIFY_PIN_DIRECT);

	PinModifyBuilder builder;
	QByteArray controlRes;
	PCSC_RETURNCODE pcscReturnCode = control(cmdID, builder.createChangeEidPinCommandData(pTimeoutSeconds), controlRes);
	if (pcscReturnCode != PcscUtils::Scard_S_Success)
	{
		qCWarning(card_pcsc) << "Modify PIN failed";
		return CardReturnCode::COMMAND_FAILED;
	}

	PinModifyOutput output;
	output.parse(controlRes);

	return output.getReturnCode();
}
