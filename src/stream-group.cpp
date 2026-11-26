#include "stream-group.hpp"

#include <plugin-support.h>

std::vector<EncoderChoice> AvailableEncoders()
{
	static const EncoderChoice candidates[] = {
		{"h264_texture_amf", "AMD AMF H.264 (GPU)"},
		{"obs_nvenc_h264_tex", "NVIDIA NVENC H.264 (GPU)"},
		{"jim_nvenc", "NVIDIA NVENC H.264 (GPU, legacy)"},
		{"obs_qsv11_v2", "Intel QuickSync H.264 (GPU)"},
		{"obs_x264", "x264 (CPU)"},
	};

	std::vector<EncoderChoice> result;
	for (const auto &c : candidates) {
		if (obs_encoder_get_display_name(c.id))
			result.push_back(c);
	}
	return result;
}

StreamGroup::StreamGroup(std::string id_, std::string label_, std::function<video_t *()> videoSource_)
	: id(std::move(id_)),
	  label(std::move(label_)),
	  videoSource(std::move(videoSource_))
{
	auto encoders = AvailableEncoders();
	if (!encoders.empty())
		encoderId = encoders.front().id;
}

StreamGroup::~StreamGroup()
{
	Stop();
}

bool StreamGroup::Active() const
{
	std::lock_guard<std::mutex> lock(statusMutex);
	return !live.empty();
}

bool StreamGroup::Start(std::string &error)
{
	if (Active())
		return true;

	video_t *video = videoSource ? videoSource() : nullptr;
	if (!video) {
		error = "The video canvas for this group is not ready.";
		return false;
	}

	size_t enabledCount = 0;
	for (const auto &d : destinations) {
		if (d.enabled && !d.server.empty() && !d.key.empty())
			enabledCount++;
	}
	if (!enabledCount) {
		error = "Add at least one destination with a server URL and stream key, and tick it.";
		return false;
	}

	if (encoderId.empty() || !obs_encoder_get_display_name(encoderId.c_str())) {
		error = "The selected video encoder is not available on this PC.";
		return false;
	}

	/* ---- encoders (shared by every destination in this group) ---- */
	obs_data_t *vs = obs_data_create();
	obs_data_set_string(vs, "rate_control", "CBR");
	obs_data_set_int(vs, "bitrate", videoBitrate);
	obs_data_set_int(vs, "keyint_sec", 2); /* TikTok/YouTube want 2s keyframes */
	obs_data_set_string(vs, "profile", "high");
	std::string vname = "Embassy " + label + " Video";
	videoEncoder = obs_video_encoder_create(encoderId.c_str(), vname.c_str(), vs, nullptr);
	obs_data_release(vs);

	obs_data_t *as = obs_data_create();
	obs_data_set_int(as, "bitrate", audioBitrate);
	std::string aname = "Embassy " + label + " Audio";
	audioEncoder = obs_audio_encoder_create("ffmpeg_aac", aname.c_str(), as, 0, nullptr);
	obs_data_release(as);

	if (!videoEncoder || !audioEncoder) {
		error = "Could not create the encoders.";
		obs_encoder_release(videoEncoder);
		obs_encoder_release(audioEncoder);
		videoEncoder = audioEncoder = nullptr;
		return false;
	}

	obs_encoder_set_video(videoEncoder, video);
	obs_encoder_set_audio(audioEncoder, obs_get_audio());

	/* ---- one RTMP output per destination ---- */
	size_t failures = 0;
	for (size_t i = 0; i < destinations.size(); i++) {
		const auto &d = destinations[i];
		if (!d.enabled || d.server.empty() || d.key.empty())
			continue;
		if (!StartDestination(i))
			failures++;
	}

	obs_log(LOG_INFO, "[%s] started %zu destination(s), %zu failed", label.c_str(), enabledCount, failures);

	if (failures == enabledCount) {
		error = "No destination could start. Check the server URLs and stream keys.";
		Stop();
		return false;
	}
	return true;
}

bool StreamGroup::StartDestination(size_t index)
{
	if (index >= destinations.size() || !videoEncoder || !audioEncoder)
		return false;

	const Destination &d = destinations[index];
	auto lo = std::make_unique<LiveOutput>();
	lo->group = this;
	lo->destIndex = index;
	lo->name = d.name.empty() ? d.server : d.name;

	obs_data_t *ss = obs_data_create();
	obs_data_set_string(ss, "server", d.server.c_str());
	obs_data_set_string(ss, "key", d.key.c_str());
	std::string sname = "Embassy " + label + " - " + lo->name;
	lo->service = obs_service_create("rtmp_custom", sname.c_str(), ss, nullptr);
	obs_data_release(ss);

	lo->output = obs_output_create("rtmp_output", sname.c_str(), nullptr, nullptr);
	if (!lo->service || !lo->output) {
		obs_log(LOG_WARNING, "[%s] %s: could not create output", label.c_str(), lo->name.c_str());
		obs_service_release(lo->service);
		obs_output_release(lo->output);
		return false;
	}

	obs_output_set_video_encoder(lo->output, videoEncoder);
	obs_output_set_audio_encoder(lo->output, audioEncoder, 0);
	obs_output_set_service(lo->output, lo->service);
	obs_output_set_reconnect_settings(lo->output, 20, 5);

	signal_handler_t *sh = obs_output_get_signal_handler(lo->output);
	signal_handler_connect(sh, "start", OnStart, lo.get());
	signal_handler_connect(sh, "stop", OnStop, lo.get());
	signal_handler_connect(sh, "reconnect", OnReconnect, lo.get());
	signal_handler_connect(sh, "reconnect_success", OnReconnectOk, lo.get());

	lo->status = "Connecting...";
	LiveOutput *raw = lo.get();
	{
		std::lock_guard<std::mutex> lock(statusMutex);
		live.push_back(std::move(lo));
	}

	if (!obs_output_start(raw->output)) {
		const char *err = obs_output_get_last_error(raw->output);
		SetStatus(raw, std::string("Failed: ") + (err && *err ? err : "could not connect"));
		return false;
	}
	return true;
}

void StreamGroup::DestroyOutput(std::unique_ptr<LiveOutput> &lo)
{
	signal_handler_t *sh = obs_output_get_signal_handler(lo->output);
	signal_handler_disconnect(sh, "start", OnStart, lo.get());
	signal_handler_disconnect(sh, "stop", OnStop, lo.get());
	signal_handler_disconnect(sh, "reconnect", OnReconnect, lo.get());
	signal_handler_disconnect(sh, "reconnect_success", OnReconnectOk, lo.get());

	obs_output_stop(lo->output);
	obs_output_release(lo->output);
	obs_service_release(lo->service);
	lo.reset();
}

void StreamGroup::ReleaseOutputs()
{
	std::vector<std::unique_ptr<LiveOutput>> old;
	{
		std::lock_guard<std::mutex> lock(statusMutex);
		old.swap(live);
	}

	for (auto &lo : old)
		DestroyOutput(lo);
}

bool StreamGroup::UpdateKey(size_t index, const std::string &key, std::string &error)
{
	if (index >= destinations.size()) {
		error = "That destination no longer exists.";
		return false;
	}
	if (key.empty()) {
		error = "The clipboard is empty. Copy the stream key first.";
		return false;
	}

	destinations[index].key = key;

	/* Not live: the new key is used next time you press Start */
	if (!Active() || !destinations[index].enabled || destinations[index].server.empty())
		return true;

	/* Live: restart only this destination, the others keep streaming */
	std::unique_ptr<LiveOutput> old;
	{
		std::lock_guard<std::mutex> lock(statusMutex);
		for (auto it = live.begin(); it != live.end(); ++it) {
			if ((*it)->destIndex == index) {
				old = std::move(*it);
				live.erase(it);
				break;
			}
		}
	}
	if (old)
		DestroyOutput(old);

	obs_log(LOG_INFO, "[%s] new stream key for destination %zu, reconnecting it", label.c_str(), index);
	if (!StartDestination(index)) {
		error = "Saved the new key, but that destination could not reconnect. Check the server URL.";
		return false;
	}
	return true;
}

void StreamGroup::Stop()
{
	ReleaseOutputs();

	obs_encoder_release(videoEncoder);
	obs_encoder_release(audioEncoder);
	videoEncoder = nullptr;
	audioEncoder = nullptr;
}

std::vector<std::string> StreamGroup::Statuses()
{
	std::lock_guard<std::mutex> lock(statusMutex);
	std::vector<std::string> out;
	for (auto &lo : live)
		out.push_back(lo->name + ": " + lo->status);
	return out;
}

void StreamGroup::SetStatus(LiveOutput *lo, const std::string &status)
{
	std::lock_guard<std::mutex> lock(statusMutex);
	lo->status = status;
}

void StreamGroup::OnStart(void *param, calldata_t *)
{
	auto *lo = static_cast<LiveOutput *>(param);
	lo->group->SetStatus(lo, "LIVE");
}

void StreamGroup::OnStop(void *param, calldata_t *cd)
{
	auto *lo = static_cast<LiveOutput *>(param);
	long long code = calldata_int(cd, "code");
	if (code == OBS_OUTPUT_SUCCESS) {
		lo->group->SetStatus(lo, "Stopped");
		return;
	}

	const char *err = calldata_string(cd, "last_error");
	std::string msg = "Disconnected";
	switch (code) {
	case OBS_OUTPUT_BAD_PATH:
		msg = "Bad server URL";
		break;
	case OBS_OUTPUT_CONNECT_FAILED:
		msg = "Could not connect";
		break;
	case OBS_OUTPUT_INVALID_STREAM:
		msg = "Stream key rejected";
		break;
	case OBS_OUTPUT_DISCONNECTED:
		msg = "Disconnected (gave up reconnecting)";
		break;
	default:
		break;
	}
	if (err && *err)
		msg += std::string(" - ") + err;
	lo->group->SetStatus(lo, msg);
}

void StreamGroup::OnReconnect(void *param, calldata_t *)
{
	auto *lo = static_cast<LiveOutput *>(param);
	lo->group->SetStatus(lo, "Reconnecting...");
}

void StreamGroup::OnReconnectOk(void *param, calldata_t *)
{
	auto *lo = static_cast<LiveOutput *>(param);
	lo->group->SetStatus(lo, "LIVE");
}

/* ------------------------------------------------------------------------ */

obs_data_t *StreamGroup::Save() const
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "encoder", encoderId.c_str());
	obs_data_set_int(data, "video_bitrate", videoBitrate);
	obs_data_set_int(data, "audio_bitrate", audioBitrate);
	obs_data_set_bool(data, "follow_main_stream", followMainStream);

	obs_data_array_t *arr = obs_data_array_create();
	for (const auto &d : destinations) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "name", d.name.c_str());
		obs_data_set_string(item, "server", d.server.c_str());
		obs_data_set_string(item, "key", d.key.c_str());
		obs_data_set_bool(item, "enabled", d.enabled);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "destinations", arr);
	obs_data_array_release(arr);
	return data;
}

void StreamGroup::Load(obs_data_t *data)
{
	if (!data)
		return;

	std::string enc = obs_data_get_string(data, "encoder");
	if (!enc.empty() && obs_encoder_get_display_name(enc.c_str()))
		encoderId = enc;

	obs_data_set_default_int(data, "video_bitrate", 6000);
	obs_data_set_default_int(data, "audio_bitrate", 160);
	obs_data_set_default_bool(data, "follow_main_stream", true);
	videoBitrate = (int)obs_data_get_int(data, "video_bitrate");
	audioBitrate = (int)obs_data_get_int(data, "audio_bitrate");
	followMainStream = obs_data_get_bool(data, "follow_main_stream");

	destinations.clear();
	obs_data_array_t *arr = obs_data_get_array(data, "destinations");
	size_t count = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(arr, i);
		Destination d;
		d.name = obs_data_get_string(item, "name");
		d.server = obs_data_get_string(item, "server");
		d.key = obs_data_get_string(item, "key");
		d.enabled = obs_data_get_bool(item, "enabled");
		destinations.push_back(d);
		obs_data_release(item);
	}
	obs_data_array_release(arr);
}
