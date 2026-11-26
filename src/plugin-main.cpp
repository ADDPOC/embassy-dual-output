/*
 * Embassy Dual Output - OBS plugin
 * Adds a portrait (9:16) canvas next to the normal landscape canvas and
 * streams both at the same time to any number of platforms.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or later.
 */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QMainWindow>

#include "plugin-state.hpp"
#include "portrait-canvas.hpp"
#include "portrait-dock.hpp"
#include "streams-dock.hpp"

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define SAVE_KEY "embassy-dual-output"
#define DOCK_PORTRAIT "embassy_portrait_canvas"
#define DOCK_STREAMS "embassy_dual_streams"

static std::unique_ptr<StreamGroup> portraitGroup;
static std::unique_ptr<StreamGroup> landscapeGroup;
static bool docksAdded = false;

StreamGroup &PortraitGroup()
{
	return *portraitGroup;
}

StreamGroup &LandscapeGroup()
{
	return *landscapeGroup;
}

/* ------------------------------------------------------------------------ */
/* Stream settings live in the plugin's own config file (not the scene      */
/* collection), so keys stay the same when you switch scene collections.    */

static std::string settings_path()
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *path = obs_module_config_path("streams.json");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

void SaveStreamSettings()
{
	if (!portraitGroup || !landscapeGroup)
		return;

	obs_data_t *data = obs_data_create();
	obs_data_t *p = portraitGroup->Save();
	obs_data_t *l = landscapeGroup->Save();
	obs_data_set_obj(data, "portrait", p);
	obs_data_set_obj(data, "landscape", l);
	obs_data_release(p);
	obs_data_release(l);

	std::string path = settings_path();
	if (!path.empty() && !obs_data_save_json_safe(data, path.c_str(), "tmp", "bak"))
		obs_log(LOG_WARNING, "Could not save %s", path.c_str());
	obs_data_release(data);
}

static void load_stream_settings()
{
	std::string path = settings_path();
	obs_data_t *data = path.empty() ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data) {
		/* First run: start with the usual setup. Twitch stays in OBS's
		 * own Stream settings. Destinations stay off until a key is
		 * pasted in. */
		portraitGroup->destinations = {{"TikTok", "", "", false}};
		landscapeGroup->destinations = {{"YouTube", "rtmp://a.rtmp.youtube.com/live2", "", false},
						{"Kick", "", "", false}};
		return;
	}

	obs_data_t *p = obs_data_get_obj(data, "portrait");
	obs_data_t *l = obs_data_get_obj(data, "landscape");
	portraitGroup->Load(p);
	landscapeGroup->Load(l);
	obs_data_release(p);
	obs_data_release(l);
	obs_data_release(data);
}

/* ------------------------------------------------------------------------ */

static void start_following_groups()
{
	for (StreamGroup *g : {portraitGroup.get(), landscapeGroup.get()}) {
		if (!g || !g->followMainStream || g->Active())
			continue;

		bool any = false;
		for (const auto &d : g->destinations)
			any = any || (d.enabled && !d.key.empty());
		if (!any)
			continue;

		std::string error;
		if (!g->Start(error))
			obs_log(LOG_WARNING, "[%s] auto-start failed: %s", g->Label().c_str(), error.c_str());
	}
}

static void stop_following_groups()
{
	for (StreamGroup *g : {portraitGroup.get(), landscapeGroup.get()}) {
		if (g && g->followMainStream && g->Active())
			g->Stop();
	}
}

static void add_docks()
{
	if (docksAdded)
		return;

	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	obs_frontend_add_dock_by_id(DOCK_PORTRAIT, "Portrait Canvas", new PortraitDock(main));
	obs_frontend_add_dock_by_id(DOCK_STREAMS, "Dual Output Streams", new StreamsDock(main));
	docksAdded = true;
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		PortraitCanvas::Get().Ensure();
		if (PortraitCanvas::Get().SceneNames().empty())
			PortraitCanvas::Get().AddScene("Portrait Scene");
		add_docks();
		break;

	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		start_following_groups();
		break;

	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		stop_following_groups();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		PortraitCanvas::Get().ClearScenes();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		if (portraitGroup)
			portraitGroup->Stop();
		if (landscapeGroup)
			landscapeGroup->Stop();
		SaveStreamSettings();

		/* Remove the docks first so the preview stops drawing before
		 * the canvas goes away. */
		if (docksAdded) {
			obs_frontend_remove_dock(DOCK_PORTRAIT);
			obs_frontend_remove_dock(DOCK_STREAMS);
			docksAdded = false;
		}
		PortraitCanvas::Get().Shutdown();
		break;

	default:
		break;
	}
}

static void on_frontend_save(obs_data_t *save_data, bool saving, void *)
{
	PortraitCanvas &pc = PortraitCanvas::Get();

	if (saving) {
		obs_data_t *data = pc.Save();
		obs_data_set_obj(save_data, SAVE_KEY, data);
		obs_data_release(data);
	} else {
		obs_data_t *data = obs_data_get_obj(save_data, SAVE_KEY);
		pc.Load(data);
		obs_data_release(data);
	}
}

bool obs_module_load(void)
{
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_frontend_add_save_callback(on_frontend_save, nullptr);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	/* Encoders from other plugins (AMF, NVENC, x264) exist by now */
	portraitGroup =
		std::make_unique<StreamGroup>("portrait", "Portrait", []() { return PortraitCanvas::Get().Video(); });
	landscapeGroup = std::make_unique<StreamGroup>("landscape", "Landscape", []() { return obs_get_video(); });
	load_stream_settings();
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	obs_frontend_remove_save_callback(on_frontend_save, nullptr);
	portraitGroup.reset();
	landscapeGroup.reset();
	obs_log(LOG_INFO, "plugin unloaded");
}

const char *obs_module_description(void)
{
	return "Portrait + landscape dual output with multistreaming";
}
