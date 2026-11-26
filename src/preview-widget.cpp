#include "preview-widget.hpp"
#include "portrait-canvas.hpp"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
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
	setMouseTracking(true);
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

		/* Resize handle: filled square on the bottom-right corner */
		const float hs = 28.0f;
		float cx = corners[2].x, cy = corners[2].y;
		gs_render_start(true);
		gs_vertex2f(cx - hs, cy - hs);
		gs_vertex2f(cx + hs, cy - hs);
		gs_vertex2f(cx - hs, cy + hs);
		gs_vertex2f(cx + hs, cy + hs);
		gs_render_stop(GS_TRISTRIP);
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

/* ------------------------------------------------------------------------ */
/* Mouse editing                                                            */

/* Axis-aligned box of an item on the canvas, whatever its alignment */
static bool item_box(obs_sceneitem_t *item, double &left, double &top, double &w, double &h)
{
	struct matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);

	double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
	const float unit[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (const auto &u : unit) {
		struct vec3 p;
		vec3_set(&p, u[0], u[1], 0.0f);
		vec3_transform(&p, &p, &transform);
		minX = std::min(minX, (double)p.x);
		minY = std::min(minY, (double)p.y);
		maxX = std::max(maxX, (double)p.x);
		maxY = std::max(maxY, (double)p.y);
	}
	left = minX;
	top = minY;
	w = maxX - minX;
	h = maxY - minY;
	return w > 0.5 && h > 0.5;
}

bool PortraitPreview::ToCanvas(const QPointF &widgetPos, double &x, double &y) const
{
	PortraitCanvas &pc = PortraitCanvas::Get();
	double dpr = devicePixelRatioF();
	double cx = width() * dpr, cy = height() * dpr;
	double cw = pc.Width(), ch = pc.Height();
	if (cw < 1 || ch < 1)
		return false;

	double scale = std::min(cx / cw, cy / ch);
	double offX = (cx - cw * scale) / 2.0;
	double offY = (cy - ch * scale) / 2.0;

	x = (widgetPos.x() * dpr - offX) / scale;
	y = (widgetPos.y() * dpr - offY) / scale;
	return true;
}

double PortraitPreview::CanvasPerWidgetPixel() const
{
	PortraitCanvas &pc = PortraitCanvas::Get();
	double dpr = devicePixelRatioF();
	double scale = std::min(width() * dpr / pc.Width(), height() * dpr / pc.Height());
	return scale > 0 ? dpr / scale : 1.0;
}

obs_sceneitem_t *PortraitPreview::SelectedSceneItem()
{
	std::lock_guard<std::mutex> lock(selectionMutex);
	if (!selected)
		return nullptr;
	obs_source_t *source = obs_weak_source_get_source(selected);
	obs_scene_t *scene = obs_scene_from_source(source);
	obs_sceneitem_t *item = scene ? obs_scene_find_sceneitem_by_id(scene, selectedId) : nullptr;
	obs_source_release(source); /* the canvas keeps the scene alive */
	return item;
}

void PortraitPreview::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}

	double x, y;
	if (!ToCanvas(event->position(), x, y))
		return;

	/* 1. Grabbing the resize handle of the selected item? */
	double grab = 40.0 * std::max(1.0, CanvasPerWidgetPixel() / 2.0);
	if (obs_sceneitem_t *sel = SelectedSceneItem()) {
		double l, t, w, h;
		if (item_box(sel, l, t, w, h) && std::abs(x - (l + w)) <= grab && std::abs(y - (t + h)) <= grab) {
			dragMode = DragMode::Resize;
			dragStartX = x;
			dragStartY = y;
			startLeft = l;
			startTop = t;
			startW = w;
			startH = h;
			return;
		}
	}

	/* 2. Otherwise pick the top-most visible item under the cursor */
	obs_scene_t *scene = PortraitCanvas::Get().GetCurrentScene();
	if (!scene)
		return;

	struct Hit {
		double x, y;
		obs_sceneitem_t *item;
	} hit{x, y, nullptr};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *h = static_cast<Hit *>(param);
			if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
				return true;
			double l, t, w, hh;
			if (item_box(item, l, t, w, hh) && h->x >= l && h->x <= l + w && h->y >= t && h->y <= t + hh)
				h->item = item; /* later items are on top, keep the last */
			return true;
		},
		&hit);

	if (hit.item) {
		SetSelectedItem(hit.item);
		emit ItemPicked((qint64)obs_sceneitem_get_id(hit.item));

		dragMode = DragMode::Move;
		dragStartX = x;
		dragStartY = y;
		obs_sceneitem_get_pos(hit.item, &startPos);
		item_box(hit.item, startLeft, startTop, startW, startH);
	} else {
		SetSelectedItem(nullptr);
		emit ItemPicked(0);
	}
	obs_scene_release(scene);
}

void PortraitPreview::mouseMoveEvent(QMouseEvent *event)
{
	double x, y;
	if (!ToCanvas(event->position(), x, y))
		return;

	if (dragMode == DragMode::None) {
		/* Show a resize cursor over the handle */
		double grab = 40.0 * std::max(1.0, CanvasPerWidgetPixel() / 2.0);
		bool onHandle = false;
		if (obs_sceneitem_t *sel = SelectedSceneItem()) {
			double l, t, w, h;
			onHandle = item_box(sel, l, t, w, h) && std::abs(x - (l + w)) <= grab &&
				   std::abs(y - (t + h)) <= grab;
		}
		setCursor(onHandle ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
		return;
	}

	obs_sceneitem_t *item = SelectedSceneItem();
	if (!item)
		return;

	PortraitCanvas &pc = PortraitCanvas::Get();
	double dx = x - dragStartX, dy = y - dragStartY;

	if (dragMode == DragMode::Move) {
		/* Where the box would be, then snap its edges/centre to the
		 * canvas edges and centre line (hold Ctrl to turn snapping off) */
		double left = startLeft + dx, top = startTop + dy;
		if (!(event->modifiers() & Qt::ControlModifier)) {
			const double snap = 15.0 * std::max(1.0, CanvasPerWidgetPixel() / 2.0);
			const double cw = pc.Width(), ch = pc.Height();
			auto snapAxis = [snap](double start, double size, double total) {
				const double targets[3][2] = {{0.0, 0.0}, {total, size}, {total / 2.0, size / 2.0}};
				for (const auto &t : targets) {
					if (std::abs(start + t[1] - t[0]) <= snap)
						return t[0] - t[1];
				}
				return start;
			};
			left = snapAxis(left, startW, cw);
			top = snapAxis(top, startH, ch);
		}

		struct vec2 pos;
		pos.x = (float)(startPos.x + (left - startLeft));
		pos.y = (float)(startPos.y + (top - startTop));
		obs_sceneitem_set_pos(item, &pos);
	} else if (dragMode == DragMode::Resize) {
		double w = std::max(20.0, startW + dx);
		double h = std::max(20.0, startH + dy);

		/* Hold Shift to keep the shape */
		if (event->modifiers() & Qt::ShiftModifier) {
			double ratio = startW / startH;
			if (w / h > ratio)
				h = w / ratio;
			else
				w = h * ratio;
		}

		struct obs_transform_info info;
		obs_sceneitem_get_info2(item, &info);
		PortraitCanvas::FitMode mode =
			info.bounds_type == OBS_BOUNDS_SCALE_OUTER ? PortraitCanvas::FitMode::Fill
			: info.bounds_type == OBS_BOUNDS_STRETCH   ? PortraitCanvas::FitMode::Stretch
								   : PortraitCanvas::FitMode::Fit;
		pc.PlaceItem(item, (float)startLeft, (float)startTop, (float)w, (float)h, mode);
	}
}

void PortraitPreview::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && dragMode != DragMode::None) {
		dragMode = DragMode::None;
		emit ItemEdited();
	}
	QWidget::mouseReleaseEvent(event);
}

void PortraitPreview::wheelEvent(QWheelEvent *event)
{
	/* Mouse wheel over the preview = make the selected source bigger/smaller */
	obs_sceneitem_t *item = SelectedSceneItem();
	if (!item) {
		QWidget::wheelEvent(event);
		return;
	}

	double l, t, w, h;
	if (!item_box(item, l, t, w, h))
		return;

	double factor = event->angleDelta().y() > 0 ? 1.05 : 1.0 / 1.05;
	double nw = std::max(20.0, w * factor), nh = std::max(20.0, h * factor);
	double cx = l + w / 2, cy = t + h / 2;

	struct obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);
	PortraitCanvas::FitMode mode = info.bounds_type == OBS_BOUNDS_SCALE_OUTER ? PortraitCanvas::FitMode::Fill
				       : info.bounds_type == OBS_BOUNDS_STRETCH   ? PortraitCanvas::FitMode::Stretch
										  : PortraitCanvas::FitMode::Fit;
	PortraitCanvas::Get().PlaceItem(item, (float)(cx - nw / 2), (float)(cy - nh / 2), (float)nw, (float)nh, mode);
	emit ItemEdited();
	event->accept();
}
