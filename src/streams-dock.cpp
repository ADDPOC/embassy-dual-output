#include "streams-dock.hpp"
#include "plugin-state.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

enum Column { ColOn, ColName, ColServer, ColKey, ColPaste, ColCount };

/* Quick-add presets. Servers left blank must be copied from that platform's
 * live dashboard (they give each account its own URL). */
static const struct {
	const char *name;
	const char *server;
} destinationPresets[] = {
	{"TikTok", ""},
	{"YouTube", "rtmp://a.rtmp.youtube.com/live2"},
	{"Twitch", "rtmp://live.twitch.tv/app"},
	{"Kick", ""},
	{"Facebook", "rtmps://live-api-s.facebook.com:443/rtmp/"},
	{"Instagram", ""},
	{"Custom RTMP", ""},
};

GroupPanel::GroupPanel(StreamGroup &group_, const QString &title, const QString &hint, QWidget *parent)
	: QWidget(parent),
	  group(group_)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *box = new QGroupBox(title, this);
	outer->addWidget(box);
	auto *v = new QVBoxLayout(box);

	auto *hintLabel = new QLabel(hint, box);
	hintLabel->setWordWrap(true);
	hintLabel->setStyleSheet("color: #999;");
	v->addWidget(hintLabel);

	auto *form = new QFormLayout();
	encoder = new QComboBox(box);
	for (const auto &e : AvailableEncoders())
		encoder->addItem(e.label, QString(e.id));
	bitrate = new QSpinBox(box);
	bitrate->setRange(500, 50000);
	bitrate->setSingleStep(500);
	bitrate->setSuffix(" kbps");
	follow = new QCheckBox("Start and stop together with OBS's Start Streaming button", box);
	form->addRow("Encoder", encoder);
	form->addRow("Video bitrate", bitrate);
	v->addLayout(form);
	v->addWidget(follow);

	table = new QTableWidget(0, ColCount, box);
	table->setHorizontalHeaderLabels({"On", "Name", "Server URL", "Stream key", ""});
	table->horizontalHeader()->setSectionResizeMode(ColOn, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Interactive);
	table->horizontalHeader()->setSectionResizeMode(ColServer, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(ColKey, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(ColPaste, QHeaderView::ResizeToContents);
	table->verticalHeader()->setVisible(false);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setMinimumHeight(120);
	v->addWidget(table);

	auto *buttons = new QHBoxLayout();
	addButton = new QPushButton("+ Destination", box);
	removeButton = new QPushButton("Remove", box);
	startStop = new QPushButton("Start", box);
	startStop->setMinimumWidth(90);
	buttons->addWidget(addButton);
	buttons->addWidget(removeButton);
	buttons->addStretch();
	buttons->addWidget(startStop);
	v->addLayout(buttons);

	status = new QLabel("Off", box);
	status->setWordWrap(true);
	status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(status);

	auto *menu = new QMenu(addButton);
	for (const auto &p : destinationPresets) {
		QString name = p.name;
		QString server = p.server;
		menu->addAction(name, this, [this, name, server]() { AddDestination(name, server); });
	}
	addButton->setMenu(menu);

	/* load values */
	int idx = encoder->findData(QString::fromStdString(group.encoderId));
	if (idx >= 0)
		encoder->setCurrentIndex(idx);
	bitrate->setValue(group.videoBitrate);
	follow->setChecked(group.followMainStream);
	BuildTable();

	connect(encoder, qOverload<int>(&QComboBox::currentIndexChanged), this, &GroupPanel::SaveFromUi);
	connect(bitrate, &QSpinBox::editingFinished, this, &GroupPanel::SaveFromUi);
	connect(follow, &QCheckBox::toggled, this, &GroupPanel::SaveFromUi);
	connect(removeButton, &QPushButton::clicked, this, &GroupPanel::RemoveDestination);
	connect(startStop, &QPushButton::clicked, this, &GroupPanel::ToggleStream);

	if (encoder->count() == 0) {
		startStop->setEnabled(false);
		status->setText("No H.264 encoder found in OBS.");
	}
}

void GroupPanel::BuildTable()
{
	building = true;
	table->setRowCount(0);

	for (const auto &d : group.destinations) {
		int row = table->rowCount();
		table->insertRow(row);

		auto *on = new QCheckBox(table);
		on->setChecked(d.enabled);
		on->setStyleSheet("margin-left: 6px;");
		table->setCellWidget(row, ColOn, on);

		auto *name = new QLineEdit(QString::fromStdString(d.name), table);
		auto *server = new QLineEdit(QString::fromStdString(d.server), table);
		server->setPlaceholderText("rtmp://... (from the platform's live dashboard)");
		auto *key = new QLineEdit(QString::fromStdString(d.key), table);
		key->setEchoMode(QLineEdit::Password);
		key->setPlaceholderText("stream key");

		table->setCellWidget(row, ColName, name);
		table->setCellWidget(row, ColServer, server);
		table->setCellWidget(row, ColKey, key);

		auto *paste = new QPushButton("Paste key", table);
		paste->setToolTip("Paste a new stream key from the clipboard.\n"
				  "Works while live: only this destination reconnects.");
		table->setCellWidget(row, ColPaste, paste);
		connect(paste, &QPushButton::clicked, this, [this, row]() { PasteKey(row); });

		connect(on, &QCheckBox::toggled, this, &GroupPanel::SaveFromUi);
		connect(name, &QLineEdit::editingFinished, this, &GroupPanel::SaveFromUi);
		connect(server, &QLineEdit::editingFinished, this, &GroupPanel::SaveFromUi);
		connect(key, &QLineEdit::editingFinished, this, &GroupPanel::SaveFromUi);
	}
	building = false;
}

void GroupPanel::SaveFromUi()
{
	if (building || group.Active())
		return;

	group.encoderId = encoder->currentData().toString().toStdString();
	group.videoBitrate = bitrate->value();
	group.followMainStream = follow->isChecked();

	group.destinations.clear();
	for (int row = 0; row < table->rowCount(); row++) {
		Destination d;
		d.enabled = qobject_cast<QCheckBox *>(table->cellWidget(row, ColOn))->isChecked();
		d.name = qobject_cast<QLineEdit *>(table->cellWidget(row, ColName))->text().trimmed().toStdString();
		d.server = qobject_cast<QLineEdit *>(table->cellWidget(row, ColServer))->text().trimmed().toStdString();
		d.key = qobject_cast<QLineEdit *>(table->cellWidget(row, ColKey))->text().trimmed().toStdString();
		group.destinations.push_back(d);
	}

	SaveStreamSettings();
}

void GroupPanel::AddDestination(const QString &name, const QString &server)
{
	SaveFromUi();
	Destination d;
	d.name = name.toStdString();
	d.server = server.toStdString();
	d.enabled = true;
	group.destinations.push_back(d);
	BuildTable();
	SaveStreamSettings();

	int row = table->rowCount() - 1;
	QWidget *focus = table->cellWidget(row, server.isEmpty() ? ColServer : ColKey);
	if (focus)
		focus->setFocus();
}

void GroupPanel::RemoveDestination()
{
	int row = table->currentRow();
	if (row < 0) {
		/* cell widgets swallow clicks, so fall back to the focused row */
		for (int r = 0; r < table->rowCount() && row < 0; r++) {
			for (int c = 0; c < ColCount; c++) {
				QWidget *w = table->cellWidget(r, c);
				if (w && w->hasFocus())
					row = r;
			}
		}
	}
	if (row < 0) {
		QMessageBox::information(this, "Remove", "Click into the destination you want to remove first.");
		return;
	}

	SaveFromUi();
	if (row < (int)group.destinations.size())
		group.destinations.erase(group.destinations.begin() + row);
	BuildTable();
	SaveStreamSettings();
}

void GroupPanel::PasteKey(int row)
{
	QString key = QGuiApplication::clipboard()->text().trimmed();
	if (key.contains(QRegularExpression("\\s")) || key.startsWith("rtmp", Qt::CaseInsensitive)) {
		QMessageBox::warning(this, "Paste key",
				     "The clipboard doesn't look like a stream key. Copy just the key and try again.");
		return;
	}

	bool wasActive = group.Active();
	if (!wasActive) {
		SaveFromUi();
		/* Pasting a key means "I want this one on" */
		if (row < (int)group.destinations.size())
			group.destinations[(size_t)row].enabled = true;
	}

	std::string error;
	bool ok = group.UpdateKey((size_t)row, key.toStdString(), error);
	SaveStreamSettings();

	building = true;
	if (auto *edit = qobject_cast<QLineEdit *>(table->cellWidget(row, ColKey)))
		edit->setText(key);
	if (!wasActive) {
		if (auto *on = qobject_cast<QCheckBox *>(table->cellWidget(row, ColOn)))
			on->setChecked(true);
	}
	building = false;

	if (!ok)
		QMessageBox::warning(this, "Paste key", QString::fromStdString(error));
	UpdateStatus();
}

void GroupPanel::ToggleStream()
{
	if (group.Active()) {
		group.Stop();
	} else {
		SaveFromUi();
		std::string error;
		if (!group.Start(error))
			QMessageBox::warning(this, "Could not start", QString::fromStdString(error));
	}
	UpdateStatus();
}

void GroupPanel::SetEditable(bool editable)
{
	encoder->setEnabled(editable);
	bitrate->setEnabled(editable);
	addButton->setEnabled(editable);
	removeButton->setEnabled(editable);

	/* While live everything is locked except the Paste key buttons */
	for (int row = 0; row < table->rowCount(); row++) {
		for (int col = 0; col < ColCount; col++) {
			if (QWidget *w = table->cellWidget(row, col))
				w->setEnabled(editable || col == ColPaste);
		}
	}
}

void GroupPanel::UpdateStatus()
{
	bool active = group.Active();
	startStop->setText(active ? "Stop" : "Start");
	SetEditable(!active);

	if (!active) {
		status->setText("Off");
		return;
	}

	QStringList lines;
	for (const auto &s : group.Statuses())
		lines << QString::fromStdString(s);
	status->setText(lines.join("\n"));
}

/* ------------------------------------------------------------------------ */

StreamsDock::StreamsDock(QWidget *parent) : QWidget(parent)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);

	auto *content = new QWidget(scroll);
	auto *v = new QVBoxLayout(content);
	scroll->setWidget(content);

	portrait = new GroupPanel(PortraitGroup(), "Portrait 9:16 (TikTok, YouTube Shorts, Reels)",
				  "Sends the Portrait Canvas. Encoded once on your GPU, then sent to every ticked "
				  "destination.",
				  content);
	landscape = new GroupPanel(LandscapeGroup(), "Extra landscape 16:9 destinations",
				   "Sends your normal OBS picture to extra platforms (Kick, YouTube, ...) so you "
				   "don't need SE.Live. Keep your main platform in OBS's normal Stream settings.",
				   content);
	v->addWidget(portrait);
	v->addWidget(landscape);
	v->addStretch();

	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, portrait, &GroupPanel::UpdateStatus);
	connect(timer, &QTimer::timeout, landscape, &GroupPanel::UpdateStatus);
	timer->start(1000);
}
