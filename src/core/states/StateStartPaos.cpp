/*!
 * \copyright Copyright (c) 2014 Governikus GmbH & Co. KG
 */

#include "CardConnection.h"
#include "StateStartPaos.h"
#include "paos/invoke/StartPaos.h"

using namespace governikus;

StateStartPaos::StateStartPaos(const QSharedPointer<WorkflowContext>& pContext)
	: AbstractGenericState(pContext, false)
{
}


void StateStartPaos::run()
{
	Q_ASSERT(getContext()->getTcToken());

	auto sessionId = getContext()->getTcToken()->getSessionIdentifier();
	getContext()->setStartPaos(QSharedPointer<StartPaos>(new StartPaos(sessionId)));

	Q_EMIT fireSuccess();
}
