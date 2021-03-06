import QtQuick 2.6
import QtQuick.Layouts 1.2

import "../global"

Rectangle {
	id: baseItem
	height: column.height

	property int headerHeight: 0
	property int textHeight: 0
	property int footerHeight: 0
	property int totalHits: providerModel.additionalResultCount

	visible: totalHits > 0

	Column {
		id: column
		width: baseItem.width

		Image {
			id: backgroundImage
			source: Category.backgroundImageSource("all")
			asynchronous: true
			height: baseItem.headerHeight
			width: parent.width
			fillMode: Image.PreserveAspectCrop
			anchors.horizontalCenter: parent.horizontalCenter

			Image {
				id: icon
				source: Category.imageSource("all")
				asynchronous: true
				height: backgroundImage.height * 0.5
				width: height
				fillMode: Image.PreserveAspectFit
				anchors.horizontalCenter: backgroundImage.horizontalCenter
				anchors.bottom: backgroundImage.bottom
				anchors.bottomMargin: Utils.dp(20)
			}
		}

		Rectangle {
			id: textRectangle
			height: baseItem.textHeight
			width: parent.width

			Text {
				text: '<html>' + qsTr("Additional results:") + "&nbsp;" + baseItem.totalHits + '</html>'

				anchors.centerIn: parent

				font.bold: true
				font.pixelSize: Constants.normal_font_size
				color: PlatformConstants.dark_grey_secondary_text
			}
		}

		Rectangle {
			height: baseItem.footerHeight
			width: parent.width
			color: Constants.blue
		}
	}

	MouseArea {
		anchors.fill: parent
		onClicked: providerModel.addAdditionalResultCategories()
	}
}
