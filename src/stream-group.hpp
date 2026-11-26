/*
 * A stream group = one video encoder + one audio encoder shared by any number
 * of RTMP destinations. Encoding happens once no matter how many platforms you
 * send to, so the GPU cost is one encode per group; only upload bandwidth
 * grows with each destination.
 */
#pragma once

#include <obs.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct Destination {
	std::string name;
	std::string server;
	std::string key;
	bool enabled = true;
};

struct EncoderChoice {
	const char *id;
	const char *label;
};

/* H.264 encoders we offer, best first. Only those installed are listed. */
std::vector<EncoderChoice> AvailableEncoders();

class StreamGroup {
public:
	StreamGroup(std::string id, std::string label, std::function<video_t *()> videoSource);
	~StreamGroup();

	const std::string &Id() const { return id; }
	const std::string &Label() const { return label; }

	/* Settings (only change while stopped) */
	std::string encoderId;
	int videoBitrate = 6000;
	int audioBitrate = 160;
	bool followMainStream = true;
	std::vector<Destination> destinations;

	bool Start(std::string &error);
	void Stop();

	/* Sets a new stream key. If the group is live, only that destination
	 * reconnects with the new key; the others keep streaming. */
	bool UpdateKey(size_t index, const std::string &key, std::string &error);
	bool Active() const;

	/* One status line per destination, safe to call from the UI thread */
	std::vector<std::string> Statuses();

	obs_data_t *Save() const;
	void Load(obs_data_t *data);

private:
	struct LiveOutput {
		StreamGroup *group = nullptr;
		size_t destIndex = 0;
		std::string name;
		obs_output_t *output = nullptr;
		obs_service_t *service = nullptr;
		std::string status;
	};

	static void OnStart(void *param, calldata_t *cd);
	static void OnStop(void *param, calldata_t *cd);
	static void OnReconnect(void *param, calldata_t *cd);
	static void OnReconnectOk(void *param, calldata_t *cd);
	void SetStatus(LiveOutput *live, const std::string &status);
	void ReleaseOutputs();
	bool StartDestination(size_t index);
	void DestroyOutput(std::unique_ptr<LiveOutput> &lo);

	std::string id;
	std::string label;
	std::function<video_t *()> videoSource;

	obs_encoder_t *videoEncoder = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	std::vector<std::unique_ptr<LiveOutput>> live;
	mutable std::mutex statusMutex;
};
