#include "portrait-canvas.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <algorithm>

#define CANVAS_NAME "Embassy Portrait"
#define DEFAULT_SCENE_NAME "Portrait Scene"

PortraitCanvas &PortraitCanvas::Get()
{
	static PortraitCanvas instance;
	return instance;
}

static void on_main_video_reset(void *param, calldata_t *)
{
	/* Main canvas was reset (user changed FPS/resolution in Settings).
	 * libobs restores our canvas with its old video info, so re-sync the
	 * frame rate with the main canvas. */
	auto *self = static_cast<PortraitCanvas *>(param);
	self->SetResolution(self->Width(), self->Height());
}

bool PortraitCanvas::Ensure()
{
	if (canvas)
		return true;

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		obs_log(LOG_WARNING, "Main video is not initialized yet, cannot create portrait canvas");
		return false;
	}

	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;

	/* ACTIVATE: sources in portrait scenes become active (cameras turn on).
	 * SCENE_REF: the canvas keeps our scenes alive.
	 * No MIX_AUDIO: audio comes from the normal OBS mixer, so sources
	 * that are in both canvases are not heard twice.
	 * EPHEMERAL: OBS itself should not try to save this canvas; we do it. */
	canvas = obs_canvas_create(CANVAS_NAME, &ovi, ACTIVATE | SCENE_REF | EPHEMERAL);
	if (!canvas) {
		obs_log(LOG_ERROR, "Failed to create portrait canvas");
		return false;
	}

	signal_handler_connect(obs_get_signal_handler(), "video_reset", on_main_video_reset, this);

	obs_log(LOG_INFO, "Portrait canvas created (%ux%u @ %u/%u fps)", width, height, ovi.fps_num, ovi.fps_den);
	return true;
}

void PortraitCanvas::Shutdown()
{
	if (!canvas)
		return;

	signal_handler_disconnect(obs_get_signal_handler(), "video_reset", on_main_video_reset, this);

	ClearScenes();
	obs_canvas_remove(canvas);
	obs_canvas_release(canvas);
	canvas = nullptr;
	listeners.clear();
}

video_t *PortraitCanvas::Video() const
{
	return canvas ? obs_canvas_get_video(canvas) : nullptr;
}

uint32_t PortraitCanvas::Width() const
{
	return width;
}

uint32_t PortraitCanvas::Height() const
{
	return height;
}

bool PortraitCanvas::SetResolution(uint32_t cx, uint32_t cy)
{
	if (!canvas) {
		width = cx;
		height = cy;
		return true;
	}

	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return false;

	ovi.base_width = cx;
	ovi.base_height = cy;
	ovi.output_width = cx;
	ovi.output_height = cy;

	if (!obs_canvas_reset_video(canvas, &ovi)) {
		obs_log(LOG_WARNING, "Could not reset portrait canvas video (is OBS streaming or recording?)");
		return false;
	}

	width = cx;
	height = cy;
	NotifyChanged();
	return true;
}

/* ------------------------------------------------------------------------ */
/* Scenes                                                                   */

std::vector<std::string> PortraitCanvas::SceneNames()
{
	std::vector<std::string> names;
	if (!canvas)
		return names;

	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<std::string> *>(param);
			if (!obs_source_is_group(source))
				list->emplace_back(obs_source_get_name(source));
			return true;
		},
		&names);

	std::sort(names.begin(), names.end());
	return names;
}

std::string PortraitCanvas::CurrentSceneName()
{
	if (!canvas)
		return {};

	obs_source_t *source = obs_canvas_get_channel(canvas, 0);
	std::string name = source ? obs_source_get_name(source) : "";
	obs_source_release(source);
	return name;
}

obs_scene_t *PortraitCanvas::GetScene(const std::string &name)
{
	if (!canvas || name.empty())
		return nullptr;
	return obs_canvas_get_scene_by_name(canvas, name.c_str());
}

obs_scene_t *PortraitCanvas::GetCurrentScene()
{
	return GetScene(CurrentSceneName());
}

bool PortraitCanvas::SetCurrentScene(const std::string &name)
{
	obs_scene_t *scene = GetScene(name);
	if (!scene)
		return false;

	obs_canvas_set_channel(canvas, 0, obs_scene_get_source(scene));
	obs_scene_release(scene);
	NotifyChanged();
	return true;
}

std::string PortraitCanvas::UniqueSceneName(const std::string &base)
{
	std::string name = base.empty() ? DEFAULT_SCENE_NAME : base;
	std::string candidate = name;
	int n = 2;

	for (;;) {
		obs_source_t *existing = obs_canvas_get_source_by_name(canvas, candidate.c_str());
		if (!existing)
			return candidate;
		obs_source_release(existing);
		candidate = name + " " + std::to_string(n++);
	}
}

std::string PortraitCanvas::AddScene(const std::string &wantedName)
{
	if (!Ensure())
		return {};

	std::string name = UniqueSceneName(wantedName);
	obs_scene_t *scene = obs_canvas_scene_create(canvas, name.c_str());
	if (!scene)
		return {};

	/* The canvas holds its own reference (SCENE_REF) */
	bool first = CurrentSceneName().empty();
	if (first)
		obs_canvas_set_channel(canvas, 0, obs_scene_get_source(scene));

	obs_scene_release(scene);
	NotifyChanged();
	return name;
}

bool PortraitCanvas::RenameScene(const std::string &oldName, const std::string &newName)
{
	if (newName.empty() || oldName == newName)
		return false;

	obs_source_t *clash = obs_canvas_get_source_by_name(canvas, newName.c_str());
	if (clash) {
		obs_source_release(clash);
		return false;
	}

	obs_scene_t *scene = GetScene(oldName);
	if (!scene)
		return false;

	obs_source_set_name(obs_scene_get_source(scene), newName.c_str());
	obs_scene_release(scene);
	NotifyChanged();
	return true;
}

bool PortraitCanvas::RemoveScene(const std::string &name)
{
	obs_scene_t *scene = GetScene(name);
	if (!scene)
		return false;

	bool wasCurrent = CurrentSceneName() == name;
	obs_source_t *source = obs_scene_get_source(scene);

	if (wasCurrent)
		obs_canvas_set_channel(canvas, 0, nullptr);

	obs_source_remove(source);
	obs_scene_release(scene);

	if (wasCurrent) {
		auto names = SceneNames();
		if (!names.empty())
			SetCurrentScene(names.front());
	}

	NotifyChanged();
	return true;
}

void PortraitCanvas::ClearScenes()
{
	if (!canvas)
		return;

	obs_canvas_set_channel(canvas, 0, nullptr);

	std::vector<obs_source_t *> scenes;
	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			static_cast<std::vector<obs_source_t *> *>(param)->push_back(obs_source_get_ref(source));
			return true;
		},
		&scenes);

	for (obs_source_t *source : scenes) {
		obs_source_remove(source);
		obs_source_release(source);
	}

	NotifyChanged();
}

/* ------------------------------------------------------------------------ */
/* Save / load                                                              */

obs_data_t *PortraitCanvas::Save()
{
	obs_data_t *data = obs_data_create();
	if (!canvas)
		return data;

	obs_data_array_t *scenes = obs_data_array_create();
	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			obs_data_t *sceneData = obs_save_source(source);
			obs_data_array_push_back(static_cast<obs_data_array_t *>(param), sceneData);
			obs_data_release(sceneData);
			return true;
		},
		scenes);

	obs_data_set_array(data, "scenes", scenes);
	obs_data_array_release(scenes);

	obs_data_set_string(data, "current_scene", CurrentSceneName().c_str());
	obs_data_set_int(data, "width", width);
	obs_data_set_int(data, "height", height);
	return data;
}

void PortraitCanvas::Load(obs_data_t *data)
{
	if (data) {
		uint32_t cx = (uint32_t)obs_data_get_int(data, "width");
		uint32_t cy = (uint32_t)obs_data_get_int(data, "height");
		if (cx >= 2 && cy >= 2 && (cx != width || cy != height))
			SetResolution(cx, cy);
	}

	if (!Ensure())
		return;

	ClearScenes();

	obs_data_array_t *scenes = data ? obs_data_get_array(data, "scenes") : nullptr;
	const char *canvasUuid = obs_canvas_get_uuid(canvas);

	std::vector<obs_source_t *> loaded;
	size_t count = scenes ? obs_data_array_count(scenes) : 0;

	/* Pass 1: create every scene in our canvas (canvas UUID changes each
	 * time OBS starts, so point the saved data at the current one). */
	for (size_t i = 0; i < count; i++) {
		obs_data_t *sceneData = obs_data_array_item(scenes, i);
		obs_data_set_string(sceneData, "canvas_uuid", canvasUuid);

		obs_source_t *source = obs_load_source(sceneData);
		if (source)
			loaded.push_back(source);
		obs_data_release(sceneData);
	}

	/* Pass 2: let scenes load their items now that all scenes exist
	 * (scenes can contain other portrait scenes). */
	for (obs_source_t *source : loaded) {
		obs_source_load2(source);
		obs_source_release(source); /* canvas keeps its own ref */
	}
	obs_data_array_release(scenes);

	std::string current = data ? obs_data_get_string(data, "current_scene") : "";
	auto names = SceneNames();

	if (names.empty()) {
		AddScene(DEFAULT_SCENE_NAME);
	} else if (!SetCurrentScene(current)) {
		SetCurrentScene(names.front());
	}

	NotifyChanged();
}

/* ------------------------------------------------------------------------ */
/* Listeners                                                                */

int PortraitCanvas::AddListener(Listener cb)
{
	int id = nextListenerId++;
	listeners.emplace_back(id, std::move(cb));
	return id;
}

void PortraitCanvas::RemoveListener(int id)
{
	listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
				       [id](const auto &entry) { return entry.first == id; }),
			listeners.end());
}

void PortraitCanvas::NotifyChanged()
{
	auto copy = listeners;
	for (auto &entry : copy)
		entry.second();
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                   */

void PortraitCanvas::PlaceItem(obs_sceneitem_t *item, float x, float y, float w, float h, FitMode mode)
{
	if (!item)
		return;

	struct obs_transform_info info = {};
	obs_sceneitem_get_info2(item, &info);

	info.pos.x = x;
	info.pos.y = y;
	info.rot = 0.0f;
	info.scale.x = 1.0f;
	info.scale.y = 1.0f;
	info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	info.bounds.x = w;
	info.bounds.y = h;
	info.bounds_alignment = OBS_ALIGN_CENTER;

	switch (mode) {
	case FitMode::Fill:
		/* Cover the whole box and trim whatever hangs over the edges */
		info.bounds_type = OBS_BOUNDS_SCALE_OUTER;
		info.crop_to_bounds = true;
		break;
	case FitMode::Fit:
		info.bounds_type = OBS_BOUNDS_SCALE_INNER;
		info.crop_to_bounds = false;
		break;
	case FitMode::Stretch:
		info.bounds_type = OBS_BOUNDS_STRETCH;
		info.crop_to_bounds = false;
		break;
	}

	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_info2(item, &info);
	obs_sceneitem_defer_update_end(item);
}
