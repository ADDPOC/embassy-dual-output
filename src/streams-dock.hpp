/*
 * "Dual Output Streams" dock: destinations, stream keys, encoder and
 * start/stop for the portrait group and the extra landscape group.
 */
#pragma once

#include <QWidget>

class QTableWidget;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QTimer;
class StreamGroup;

class GroupPanel : public QWidget {
	Q_OBJECT

public:
	GroupPanel(StreamGroup &group, const QString &title, const QString &hint, QWidget *parent = nullptr);

public slots:
	void UpdateStatus();

private slots:
	void ToggleStream();
	void AddDestination(const QString &name, const QString &server);
	void RemoveDestination();
	void PasteKey(int row);
	void SaveFromUi();

private:
	void BuildTable();
	void SetEditable(bool editable);

	StreamGroup &group;
	QComboBox *encoder = nullptr;
	QSpinBox *bitrate = nullptr;
	QCheckBox *follow = nullptr;
	QTableWidget *table = nullptr;
	QPushButton *startStop = nullptr;
	QPushButton *addButton = nullptr;
	QPushButton *removeButton = nullptr;
	QLabel *status = nullptr;
	bool building = false;
};

class StreamsDock : public QWidget {
	Q_OBJECT

public:
	explicit StreamsDock(QWidget *parent = nullptr);

private:
	GroupPanel *portrait = nullptr;
	GroupPanel *landscape = nullptr;
	QTimer *timer = nullptr;
};
