#include "preview-widget.hpp"
#include "portrait-canvas.hpp"

#include <QResizeEvent>
#include <QShowEvent>
#include <QWindow>

#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <mutex>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

static std::mutex selectionMutex;

PortraitPreview::PortraitPreview(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumSize(90, 160);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

PortraitPreview::~PortraitPreview()
{
	DestroyDisplay();
	std::lock_guard<std::mutex> lock(selectionMutex);
	obs_weak_source_release(selected);
	selected = nullptr;
}

void PortraitPreview::SetSelectedItem(obs_sceneitem_t *item)
{
	std::lock_guard<std::mutex> lock(selectionMutex);
	obs_weak_source_release(selected);
	selected = nullptr;
	selectedId = 0;

	if (item) {
		obs_scene_t *scene = obs_sceneitem_get_scene(item);
		selected = obs_source_get_weak_source(obs_scene_get_source(scene));
		selectedId = obs_sceneitem_get_id(item);
	}
}

void PortraitPreview::CreateDisplay()
{
	if (display || !windowHandle())
		return;

	QSize size = this->size() * devicePixelRatioF();

	struct gs_init_data info = {};
	info.cx = (uint32_t)std::max(size.width(), 1);
	info.cy = (uint32_t)std::max(size.height(), 1);
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

#if defined(_WIN32)
	info.window.hwnd = reinterpret_cast<void *>(winId());
#elif defined(__APPLE__)
	info.window.view = (id)winId();
#else
	info.window.id = (uint32_t)winId();
	info.window.display = obs_get_nix_platform_display();
#endif

	display = obs_display_create(&info, 0x111111);
	if (display)
		obs_display_add_draw_callback(display, PortraitPreview::Draw, this);
}

void PortraitPreview::DestroyDisplay()
{
	if (!display)
		return;
	obs_display_remove_draw_callback(display, PortraitPreview::Draw, this);
	obs_display_destroy(display);
	display = nullptr;
}

void PortraitPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	CreateDisplay();
}

void PortraitPreview::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	CreateDisplay();

	if (display && isVisible()) {
		QSize size = event->size() * devicePixelRatioF();
		obs_display_resize(display, (uint32_t)std::max(size.width(), 1), (uint32_t)std::max(size.height(), 1));
	}
}

void PortraitPreview::paintEvent(QPaintEvent *event)
{
	/* OBS draws this widget itself */
	UNUSED_PARAMETER(event);
	CreateDisplay();
}

bool PortraitPreview::event(QEvent *event)
{
	/* Docks get re-parented when they float/dock again, which destroys the
	 * native window. Rebuild the display when that happens. */
	if (event->type() == QEvent::WinIdChange || event->type() == QEvent::ParentChange) {
		DestroyDisplay();
		if (isVisible())
			CreateDisplay();
	}
	return QWidget::event(event);
}

static void draw_box(obs_sceneitem_t *item)
{
	struct matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);

	struct vec3 corners[5];
	const float unit[5][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
	for (int i = 0; i < 5; i++) {
		vec3_set(&corners[i], unit[i][0], unit[i][1], 0.0f);
		vec3_transform(&corners[i], &corners[i], &transform);
	}

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	struct vec4 red;
	vec4_set(&red, 1.0f, 0.25f, 0.25f, 1.0f);
	gs_effect_set_vec4(color, &red);

	while (gs_effect_loop(solid, "Solid")) {
		/* Draw the outline a few times, slightly offset, so it stays
		 * visible after the preview is scaled down. */
		for (int offset = -3; offset <= 3; offset += 3) {
			gs_render_start(true);
			for (int i = 0; i < 5; i++)
				gs_vertex2f(corners[i].x + (float)offset, corners[i].y + (float)offset);
			gs_render_stop(GS_LINESTRIP);
		}
	}
}

void PortraitPreview::Draw(void *param, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<PortraitPreview *>(param);
	PortraitCanvas &pc = PortraitCanvas::Get();
	obs_canvas_t *canvas = pc.Canvas();
	if (!canvas)
		return;

	struct obs_video_info ovi;
	if (!obs_canvas_get_video_info(canvas, &ovi))
		return;

	/* Letterbox the 9:16 picture inside the widget */
	double scale = std::min((double)cx / ovi.base_width, (double)cy / ovi.base_height);
	int w = (int)(ovi.base_width * scale);
	int h = (int)(ovi.base_height * scale);
	int x = ((int)cx - w) / 2;
	int y = ((int)cy - h) / 2;

	gs_viewport_push();
	gs_projection_push();

	gs_ortho(0.0f, (float)ovi.base_width, 0.0f, (float)ovi.base_height, -100.0f, 100.0f);
	gs_set_viewport(x, y, w, h);

	obs_render_canvas_texture(canvas);

	{
		std::lock_guard<std::mutex> lock(selectionMutex);
		if (self->selected) {
			obs_source_t *source = obs_weak_source_get_source(self->selected);
			obs_scene_t *scene = obs_scene_from_source(source);
			obs_sceneitem_t *item = scene ? obs_scene_find_sceneitem_by_id(scene, self->selectedId)
						      : nullptr;
			if (item)
				draw_box(item);
			obs_source_release(source);
		}
	}

	gs_projection_pop();
	gs_viewport_pop();
}
