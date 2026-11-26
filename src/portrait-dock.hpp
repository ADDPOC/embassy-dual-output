/*
 * "Portrait Canvas" dock: live 9:16 preview, portrait scene list, the sources
 * in the selected scene, and quick layout tools.
 */
#pragma once

#include <QWidget>

#include <obs.h>

class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QComboBox;
class QPushButton;
class PortraitPreview;

class PortraitDock : public QWidget {
	Q_OBJECT

public:
	explicit PortraitDock(QWidget *parent = nullptr);
	~PortraitDock() override;

public slots:
	void RefreshScenes();
	void RefreshItems();

private slots:
	void SceneClicked(QListWidgetItem *item);
	void AddScene();
	void RenameScene();
	void RemoveScene();

	void ItemSelectionChanged();
	void ItemChanged(QListWidgetItem *item);
	void ShowAddSourceMenu();
	void RemoveItem();
	void MoveItem(int direction);
	void OpenProperties();
	void OpenFilters();

	void ApplyPreset(int preset);
	void ApplyCustomLayout();
	void LoadCustomFromItem();
	void ResolutionChanged(int index);

private:
	obs_sceneitem_t *SelectedItem(); /* borrowed pointer, valid on UI thread */
	void ConnectSceneSignals();
	void DisconnectSceneSignals();
	static void SceneSignal(void *param, calldata_t *cd);

	PortraitPreview *preview = nullptr;
	QListWidget *sceneList = nullptr;
	QListWidget *itemList = nullptr;
	QComboBox *resolution = nullptr;

	QSpinBox *boxX = nullptr;
	QSpinBox *boxY = nullptr;
	QSpinBox *boxW = nullptr;
	QSpinBox *boxH = nullptr;
	QComboBox *fitMode = nullptr;

	int listenerId = 0;
	bool refreshing = false;
	obs_weak_source_t *watchedScene = nullptr;
	obs_scene_t *editScene = nullptr; /* strong ref to scene shown in item list */
};
