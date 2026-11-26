/*
 * Embassy Dual Output - portrait (9:16) canvas manager
 *
 * Owns a second OBS video canvas (default 1080x1920) that runs alongside the
 * normal landscape canvas. The portrait canvas has its own scenes. Those
 * scenes can hold any existing OBS source (camera, game capture, browser
 * alerts) or even a whole landscape scene.
 */
#pragma once

#include <obs.h>

#include <functional>
#include <string>
#include <vector>

class PortraitCanvas {
public:
	static PortraitCanvas &Get();

	/* Creates the canvas if it does not exist yet. Safe to call many times. */
	bool Ensure();
	/* Tears everything down (OBS exit). */
	void Shutdown();

	obs_canvas_t *Canvas() const { return canvas; }
	video_t *Video() const;
	uint32_t Width() const;
	uint32_t Height() const;

	/* Resolution. Only works while nothing in OBS is streaming/recording. */
	bool SetResolution(uint32_t cx, uint32_t cy);

	/* Scenes */
	std::vector<std::string> SceneNames();
	std::string CurrentSceneName();
	bool SetCurrentScene(const std::string &name);
	/* Returns a new reference (release with obs_scene_release) or nullptr */
	obs_scene_t *GetScene(const std::string &name);
	obs_scene_t *GetCurrentScene();
	std::string AddScene(const std::string &wantedName);
	bool RenameScene(const std::string &oldName, const std::string &newName);
	bool RemoveScene(const std::string &name);
	void ClearScenes();

	/* Scene collection save/load (called from the frontend save callback) */
	obs_data_t *Save();
	void Load(obs_data_t *data);

	/* UI listeners get told when scenes change */
	using Listener = std::function<void()>;
	int AddListener(Listener cb);
	void RemoveListener(int id);
	void NotifyChanged();

	/* Layout helpers: places an item inside a box on the portrait canvas */
	enum class FitMode { Fill, Fit, Stretch };
	void PlaceItem(obs_sceneitem_t *item, float x, float y, float w, float h, FitMode mode);

private:
	PortraitCanvas() = default;
	std::string UniqueSceneName(const std::string &base);

	obs_canvas_t *canvas = nullptr;
	uint32_t width = 1080;
	uint32_t height = 1920;
	std::vector<std::pair<int, Listener>> listeners;
	int nextListenerId = 1;
};
