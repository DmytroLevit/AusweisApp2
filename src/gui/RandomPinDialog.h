/*!
 * RandomPinDialog.h
 *
 * \brief Dialog for display the random PIN.
 *
 * \copyright Copyright (c) 2014 Governikus GmbH & Co. KG
 */

#pragma once

#include <QDialog>
#include <QScopedPointer>

namespace Ui
{
class RandomPinDialog;
}

namespace governikus
{

class RandomPinDialog
	: public QDialog
{
	Q_OBJECT

	public:
		RandomPinDialog(int pLength, const QString& pSelectedReader, QWidget* pParent = nullptr);
		virtual ~RandomPinDialog();

		QString getPin();

	protected:
		virtual bool eventFilter(QObject* pObject, QEvent* pEvent) override;

	private:
		QScopedPointer<Ui::RandomPinDialog> mUi;
		const QString mSelectedReader;

		void initComponents();
		void createButton();

	private Q_SLOTS:
		void onPosButtonClicked();
		void onCardRemoved(const QString& pReaderName);
		void onCardInserted();
};

} /* namespace governikus */
