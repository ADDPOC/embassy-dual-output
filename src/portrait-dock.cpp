#include "portrait-dock.hpp"
#include "portrait-canvas.hpp"
#include "preview-widget.hpp"

#include <obs-frontend-api.h>

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

enum Preset { FullFill, FullFit, TopHalf, BottomHalf, TopThird, BottomTwoThirds };

static QPushButton *small_button(const QString &text, const QString &tip, QWidget *parent)
{
	auto *b = new QPushButton(text, parent);
	b->setToolTip(tip);
	b->setAutoDefault(false);
	return b;
}

PortraitDock::PortraitDock(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);

	/* ---------------- preview ---------------- */
	preview = new PortraitPreview(this);
	auto *splitter = new QSplitter(Qt::Vertical, this);
	splitter->addWidget(preview);

	auto *controls = new QWidget(splitter);
	auto *cl = new QVBoxLayout(controls);
	cl->setContentsMargins(0, 0, 0, 0);
	splitter->addWidget(controls);
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);
	root->addWidget(splitter);

	/* ---------------- scenes ---------------- */
	auto *scenesBox = new QGroupBox("Portrait scenes (click to go live)", controls);
	auto *sl = new QVBoxLayout(scenesBox);
	sceneList = new QListWidget(scenesBox);
	sceneList->setMaximumHeight(110);
	sl->addWidget(sceneList);

	auto *sceneButtons = new QHBoxLayout();
	auto *addScene = small_button("+ Scene", "Add a portrait scene", scenesBox);
	auto *renameScene = small_button("Rename", "Rename the selected scene", scenesBox);
	auto *removeScene = small_button("Delete", "Delete the selected scene", scenesBox);
	resolution = new QComboBox(scenesBox);
	resolution->addItem("1080x1920", QSize(1080, 1920));
	resolution->addItem("720x1280", QSize(720, 1280));
	resolution->setToolTip("Portrait canvas size. Can only change while OBS is not streaming or recording.");
	auto *saveButton = small_button("Save", "Save all portrait scenes and layouts now", scenesBox);
	saveButton->setStyleSheet("font-weight: bold;");
	sceneButtons->addWidget(addScene);
	sceneButtons->addWidget(renameScene);
	sceneButtons->addWidget(removeScene);
	sceneButtons->addWidget(saveButton);
	sceneButtons->addStretch();
	sceneButtons->addWidget(resolution);
	sl->addLayout(sceneButtons);
	cl->addWidget(scenesBox);

	/* ---------------- sources ---------------- */
	auto *itemsBox = new QGroupBox("Sources in this scene (top of list = in front)", controls);
	auto *il = new QVBoxLayout(itemsBox);
	itemList = new QListWidget(itemsBox);
	itemList->setMaximumHeight(130);
	il->addWidget(itemList);

	auto *itemButtons = new QHBoxLayout();
	auto *addItem = small_button("+ Source", "Add an existing OBS source or landscape scene", itemsBox);
	auto *removeItem =
		small_button("Remove", "Remove from this portrait scene (the source itself is kept)", itemsBox);
	auto *up = small_button("Up", "Move forward", itemsBox);
	auto *down = small_button("Down", "Move backward", itemsBox);
	auto *props = small_button("Properties", "Open source properties", itemsBox);
	auto *filters = small_button("Filters", "Open source filters", itemsBox);
	itemButtons->addWidget(addItem);
	itemButtons->addWidget(removeItem);
	itemButtons->addWidget(up);
	itemButtons->addWidget(down);
	itemButtons->addWidget(props);
	itemButtons->addWidget(filters);
	il->addLayout(itemButtons);
	cl->addWidget(itemsBox);

	/* ---------------- layout ---------------- */
	auto *layoutBox = new QGroupBox(
		"Layout (drag to move, corner square or wheel to resize; Shift = keep shape, Alt = from centre)",
		controls);
	auto *ll = new QVBoxLayout(layoutBox);

	auto *presets = new QGridLayout();
	struct {
		const char *text;
		Preset preset;
	} presetButtons[] = {
		{"Full screen (fill)", FullFill},
		{"Full screen (fit)", FullFit},
		{"Top half", TopHalf},
		{"Bottom half", BottomHalf},
		{"Top third (cam)", TopThird},
		{"Bottom two-thirds", BottomTwoThirds},
	};
	int i = 0;
	for (auto &p : presetButtons) {
		auto *b = small_button(p.text, "", layoutBox);
		Preset preset = p.preset;
		connect(b, &QPushButton::clicked, this, [this, preset]() { ApplyPreset(preset); });
		presets->addWidget(b, i / 2, i % 2);
		i++;
	}
	ll->addLayout(presets);

	auto *custom = new QHBoxLayout();
	auto makeSpin = [&](const QString &label) {
		auto *s = new QSpinBox(layoutBox);
		s->setRange(-4000, 8000);
		s->setPrefix(label + " ");
		custom->addWidget(s);
		return s;
	};
	boxX = makeSpin("X");
	boxY = makeSpin("Y");
	boxW = makeSpin("W");
	boxH = makeSpin("H");
	ll->addLayout(custom);

	auto *customRow = new QHBoxLayout();
	fitMode = new QComboBox(layoutBox);
	fitMode->addItem("Fill box (crop)");
	fitMode->addItem("Fit inside box");
	fitMode->addItem("Stretch");
	auto *apply = small_button("Apply box", "Place the source inside this box", layoutBox);
	customRow->addWidget(fitMode);
	customRow->addWidget(apply);
	ll->addLayout(customRow);
	cl->addWidget(layoutBox);
	cl->addStretch();

	/* ---------------- wiring ---------------- */
	connect(saveButton, &QPushButton::clicked, this, [saveButton]() {
		obs_frontend_save();
		saveButton->setText("Saved \u2713");
		QTimer::singleShot(1500, saveButton, [saveButton]() { saveButton->setText("Save"); });
	});
	connect(sceneList, &QListWidget::itemClicked, this, &PortraitDock::SceneClicked);
	connect(addScene, &QPushButton::clicked, this, &PortraitDock::AddScene);
	connect(renameScene, &QPushButton::clicked, this, &PortraitDock::RenameScene);
	connect(removeScene, &QPushButton::clicked, this, &PortraitDock::RemoveScene);
	connect(resolution, qOverload<int>(&QComboBox::activated), this, &PortraitDock::ResolutionChanged);

	connect(itemList, &QListWidget::itemSelectionChanged, this, &PortraitDock::ItemSelectionChanged);
	connect(itemList, &QListWidget::itemChanged, this, &PortraitDock::ItemChanged);
	connect(addItem, &QPushButton::clicked, this, &PortraitDock::ShowAddSourceMenu);
	connect(removeItem, &QPushButton::clicked, this, &PortraitDock::RemoveItem);
	connect(up, &QPushButton::clicked, this, [this]() { MoveItem(1); });
	connect(down, &QPushButton::clicked, this, [this]() { MoveItem(-1); });
	connect(props, &QPushButton::clicked, this, &PortraitDock::OpenProperties);
	connect(filters, &QPushButton::clicked, this, &PortraitDock::OpenFilters);
	connect(apply, &QPushButton::clicked, this, &PortraitDock::ApplyCustomLayout);

	/* Clicking / dragging in the preview */
	connect(preview, &PortraitPreview::ItemPicked, this, [this](qint64 id) {
		for (int r = 0; r < itemList->count(); r++) {
			if (itemList->item(r)->data(Qt::UserRole).toLongLong() == id) {
				itemList->setCurrentRow(r);
				return;
			}
		}
		itemList->setCurrentRow(-1);
		itemList->clearSelection();
	});
	connect(preview, &PortraitPreview::ItemEdited, this, [this]() {
		LoadCustomFromItem();
		obs_frontend_save();
	});

	listenerId = PortraitCanvas::Get().AddListener(
		[this]() { QMetaObject::invokeMethod(this, "RefreshScenes", Qt::QueuedConnection); });

	RefreshScenes();
}

PortraitDock::~PortraitDock()
{
	PortraitCanvas::Get().RemoveListener(listenerId);
	DisconnectSceneSignals();
}

/* ------------------------------------------------------------------------ */
/* Scenes                                                                   */

void PortraitDock::RefreshScenes()
{
	PortraitCanvas &pc = PortraitCanvas::Get();

	refreshing = true;
	sceneList->clear();
	std::string current = pc.CurrentSceneName();
	for (const auto &name : pc.SceneNames()) {
		auto *item = new QListWidgetItem(QString::fromStdString(name), sceneList);
		if (name == current) {
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
			item->setText(QString::fromStdString(name) + "   [LIVE]");
			sceneList->setCurrentItem(item);
		}
		item->setData(Qt::UserRole, QString::fromStdString(name));
	}

	QSize res((int)pc.Width(), (int)pc.Height());
	int idx = resolution->findData(res);
	if (idx >= 0)
		resolution->setCurrentIndex(idx);
	refreshing = false;

	ConnectSceneSignals();
	RefreshItems();
}

void PortraitDock::SceneClicked(QListWidgetItem *item)
{
	if (!item || refreshing)
		return;
	PortraitCanvas::Get().SetCurrentScene(item->data(Qt::UserRole).toString().toStdString());
}

void PortraitDock::AddScene()
{
	bool ok = false;
	QString name = QInputDialog::getText(this, "New portrait scene", "Scene name:", QLineEdit::Normal,
					     "Portrait Scene", &ok);
	if (!ok)
		return;

	PortraitCanvas &pc = PortraitCanvas::Get();
	std::string created = pc.AddScene(name.trimmed().toStdString());
	if (!created.empty())
		pc.SetCurrentScene(created);
	obs_frontend_save();
}

void PortraitDock::RenameScene()
{
	QListWidgetItem *item = sceneList->currentItem();
	if (!item)
		return;

	QString old = item->data(Qt::UserRole).toString();
	bool ok = false;
	QString name = QInputDialog::getText(this, "Rename portrait scene", "New name:", QLineEdit::Normal, old, &ok);
	if (!ok || name.trimmed().isEmpty())
		return;

	if (!PortraitCanvas::Get().RenameScene(old.toStdString(), name.trimmed().toStdString()))
		QMessageBox::warning(this, "Rename", "That name is already used by another portrait scene.");
	obs_frontend_save();
}

void PortraitDock::RemoveScene()
{
	QListWidgetItem *item = sceneList->currentItem();
	if (!item)
		return;

	PortraitCanvas &pc = PortraitCanvas::Get();
	if (pc.SceneNames().size() <= 1) {
		QMessageBox::information(this, "Delete", "You need at least one portrait scene.");
		return;
	}

	QString name = item->data(Qt::UserRole).toString();
	if (QMessageBox::question(this, "Delete scene", "Delete portrait scene \"" + name + "\"?") != QMessageBox::Yes)
		return;

	pc.RemoveScene(name.toStdString());
	obs_frontend_save();
}

void PortraitDock::ResolutionChanged(int index)
{
	QSize size = resolution->itemData(index).toSize();
	PortraitCanvas &pc = PortraitCanvas::Get();
	if (size.width() == (int)pc.Width() && size.height() == (int)pc.Height())
		return;

	if (!pc.SetResolution((uint32_t)size.width(), (uint32_t)size.height())) {
		QMessageBox::warning(this, "Resolution",
				     "Stop all streams, recordings and the virtual camera first, then try again.");
		RefreshScenes();
		return;
	}
	obs_frontend_save();
}

/* ------------------------------------------------------------------------ */
/* Items                                                                    */

void PortraitDock::SceneSignal(void *param, calldata_t *)
{
	/* Fired from any thread: hop to the UI thread */
	QMetaObject::invokeMethod(static_cast<PortraitDock *>(param), "RefreshItems", Qt::QueuedConnection);
}

static const char *sceneSignals[] = {"item_add", "item_remove", "reorder", "refresh", "item_visible"};

void PortraitDock::ConnectSceneSignals()
{
	DisconnectSceneSignals();

	editScene = PortraitCanvas::Get().GetCurrentScene(); /* keeps a ref */
	if (!editScene)
		return;

	obs_source_t *source = obs_scene_get_source(editScene);
	watchedScene = obs_source_get_weak_source(source);
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	for (const char *sig : sceneSignals)
		signal_handler_connect(sh, sig, SceneSignal, this);
}

void PortraitDock::DisconnectSceneSignals()
{
	if (editScene) {
		signal_handler_t *sh = obs_source_get_signal_handler(obs_scene_get_source(editScene));
		for (const char *sig : sceneSignals)
			signal_handler_disconnect(sh, sig, SceneSignal, this);
		obs_scene_release(editScene);
		editScene = nullptr;
	}
	obs_weak_source_release(watchedScene);
	watchedScene = nullptr;
}

void PortraitDock::RefreshItems()
{
	int64_t keepId = 0;
	if (obs_sceneitem_t *sel = SelectedItem())
		keepId = obs_sceneitem_get_id(sel);

	refreshing = true;
	itemList->clear();

	if (editScene) {
		struct Ctx {
			QListWidget *list;
		} ctx{itemList};

		obs_scene_enum_items(
			editScene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				auto *c = static_cast<Ctx *>(param);
				obs_source_t *src = obs_sceneitem_get_source(item);
				auto *li = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(src)));
				li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
				li->setCheckState(obs_sceneitem_visible(item) ? Qt::Checked : Qt::Unchecked);
				li->setData(Qt::UserRole, (qlonglong)obs_sceneitem_get_id(item));
				li->setToolTip("Tick = visible");
				c->list->insertItem(0, li); /* top of stack first */
				return true;
			},
			&ctx);
	}

	for (int i = 0; i < itemList->count(); i++) {
		if (itemList->item(i)->data(Qt::UserRole).toLongLong() == keepId) {
			itemList->setCurrentRow(i);
			break;
		}
	}
	refreshing = false;
	ItemSelectionChanged();
}

obs_sceneitem_t *PortraitDock::SelectedItem()
{
	QListWidgetItem *li = itemList ? itemList->currentItem() : nullptr;
	if (!li || !editScene)
		return nullptr;
	return obs_scene_find_sceneitem_by_id(editScene, li->data(Qt::UserRole).toLongLong());
}

void PortraitDock::ItemSelectionChanged()
{
	if (refreshing)
		return;
	obs_sceneitem_t *item = SelectedItem();
	preview->SetSelectedItem(item);
	LoadCustomFromItem();
}

void PortraitDock::ItemChanged(QListWidgetItem *li)
{
	if (refreshing || !li || !editScene)
		return;
	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(editScene, li->data(Qt::UserRole).toLongLong());
	if (item) {
		obs_sceneitem_set_visible(item, li->checkState() == Qt::Checked);
		obs_frontend_save();
	}
}

void PortraitDock::ShowAddSourceMenu()
{
	if (!editScene) {
		QMessageBox::information(this, "Add source", "Create a portrait scene first.");
		return;
	}

	QMenu menu(this);

	std::vector<std::string> sources, scenes;

	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			uint32_t flags = obs_source_get_output_flags(src);
			if (flags & OBS_SOURCE_VIDEO)
				static_cast<std::vector<std::string> *>(param)->emplace_back(obs_source_get_name(src));
			return true;
		},
		&sources);
	obs_enum_scenes(
		[](void *param, obs_source_t *src) {
			if (!obs_source_is_group(src))
				static_cast<std::vector<std::string> *>(param)->emplace_back(obs_source_get_name(src));
			return true;
		},
		&scenes);

	std::sort(sources.begin(), sources.end());
	std::sort(scenes.begin(), scenes.end());

	QMenu *srcMenu = menu.addMenu("Source");
	for (const auto &name : sources)
		srcMenu->addAction(QString::fromStdString(name))->setData(QString::fromStdString(name));
	if (sources.empty())
		srcMenu->addAction("(Create sources in the main OBS Sources list first)")->setEnabled(false);

	QMenu *sceneMenu = menu.addMenu("Whole landscape scene");
	for (const auto &name : scenes)
		sceneMenu->addAction(QString::fromStdString(name))->setData(QString::fromStdString(name));

	QAction *chosen = menu.exec(QCursor::pos());
	if (!chosen || chosen->data().toString().isEmpty())
		return;

	std::string name = chosen->data().toString().toStdString();
	obs_source_t *source = obs_get_source_by_name(name.c_str());
	if (!source)
		return;

	obs_sceneitem_t *item = obs_scene_add(editScene, source);
	obs_source_release(source);
	if (!item)
		return;

	/* New sources start full screen, cropped to fit */
	PortraitCanvas &pc = PortraitCanvas::Get();
	pc.PlaceItem(item, 0, 0, (float)pc.Width(), (float)pc.Height(), PortraitCanvas::FitMode::Fill);

	int64_t newId = obs_sceneitem_get_id(item);
	RefreshItems();
	for (int i = 0; i < itemList->count(); i++) {
		if (itemList->item(i)->data(Qt::UserRole).toLongLong() == newId)
			itemList->setCurrentRow(i);
	}
	obs_frontend_save();
}

void PortraitDock::RemoveItem()
{
	obs_sceneitem_t *item = SelectedItem();
	if (!item)
		return;
	obs_sceneitem_remove(item);
	obs_frontend_save();
}

void PortraitDock::MoveItem(int direction)
{
	obs_sceneitem_t *item = SelectedItem();
	if (!item)
		return;
	obs_sceneitem_set_order(item, direction > 0 ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
	obs_frontend_save();
}

void PortraitDock::OpenProperties()
{
	if (obs_sceneitem_t *item = SelectedItem())
		obs_frontend_open_source_properties(obs_sceneitem_get_source(item));
}

void PortraitDock::OpenFilters()
{
	if (obs_sceneitem_t *item = SelectedItem())
		obs_frontend_open_source_filters(obs_sceneitem_get_source(item));
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                   */

void PortraitDock::ApplyPreset(int preset)
{
	obs_sceneitem_t *item = SelectedItem();
	if (!item) {
		QMessageBox::information(this, "Layout", "Select a source in the list first.");
		return;
	}

	PortraitCanvas &pc = PortraitCanvas::Get();
	float w = (float)pc.Width();
	float h = (float)pc.Height();
	using FM = PortraitCanvas::FitMode;

	switch (preset) {
	case FullFill:
		pc.PlaceItem(item, 0, 0, w, h, FM::Fill);
		break;
	case FullFit:
		pc.PlaceItem(item, 0, 0, w, h, FM::Fit);
		break;
	case TopHalf:
		pc.PlaceItem(item, 0, 0, w, h / 2, FM::Fill);
		break;
	case BottomHalf:
		pc.PlaceItem(item, 0, h / 2, w, h / 2, FM::Fill);
		break;
	case TopThird:
		pc.PlaceItem(item, 0, 0, w, h / 3, FM::Fill);
		break;
	case BottomTwoThirds:
		pc.PlaceItem(item, 0, h / 3, w, h * 2 / 3, FM::Fill);
		break;
	default:
		break;
	}

	LoadCustomFromItem();
	obs_frontend_save();
}

void PortraitDock::ApplyCustomLayout()
{
	obs_sceneitem_t *item = SelectedItem();
	if (!item)
		return;

	if (boxW->value() < 2 || boxH->value() < 2) {
		QMessageBox::information(this, "Layout", "Width and height must be at least 2.");
		return;
	}

	auto mode = static_cast<PortraitCanvas::FitMode>(fitMode->currentIndex());
	PortraitCanvas::Get().PlaceItem(item, (float)boxX->value(), (float)boxY->value(), (float)boxW->value(),
					(float)boxH->value(), mode);
	obs_frontend_save();
}

void PortraitDock::LoadCustomFromItem()
{
	obs_sceneitem_t *item = SelectedItem();
	if (!item)
		return;

	struct obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);

	if (info.bounds_type == OBS_BOUNDS_NONE) {
		/* Item was never boxed by us: show its natural size */
		obs_source_t *src = obs_sceneitem_get_source(item);
		boxX->setValue((int)info.pos.x);
		boxY->setValue((int)info.pos.y);
		boxW->setValue((int)((float)obs_source_get_width(src) * info.scale.x));
		boxH->setValue((int)((float)obs_source_get_height(src) * info.scale.y));
	} else {
		boxX->setValue((int)info.pos.x);
		boxY->setValue((int)info.pos.y);
		boxW->setValue((int)info.bounds.x);
		boxH->setValue((int)info.bounds.y);
		fitMode->setCurrentIndex(info.bounds_type == OBS_BOUNDS_SCALE_OUTER   ? 0
					 : info.bounds_type == OBS_BOUNDS_SCALE_INNER ? 1
										      : 2);
	}
}
