/*
 * Live preview of the portrait canvas, drawn by OBS directly into a native
 * window (same technique OBS uses for its own preview).
 */
#pragma once

#include <QWidget>

#include <obs.h>

class PortraitPreview : public QWidget {
	Q_OBJECT

public:
	explicit PortraitPreview(QWidget *parent = nullptr);
	~PortraitPreview() override;

	QPaintEngine *paintEngine() const override { return nullptr; }
	QSize sizeHint() const override { return QSize(270, 480); }

	/* Draws a highlight box around this item (or nothing if null) */
	void SetSelectedItem(obs_sceneitem_t *item);

signals:
	/* User clicked a source in the preview (id of the scene item, 0 = none) */
	void ItemPicked(qint64 id);
	/* User finished moving/resizing the selected source */
	void ItemEdited();

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	bool event(QEvent *event) override;

private:
	void CreateDisplay();
	void DestroyDisplay();
	static void Draw(void *param, uint32_t cx, uint32_t cy);

	/* Converts a widget position to portrait canvas pixels */
	bool ToCanvas(const QPointF &widgetPos, double &x, double &y) const;
	double CanvasPerWidgetPixel() const;
	obs_sceneitem_t *SelectedSceneItem(); /* borrowed, UI thread only */

	enum class DragMode { None, Move, Resize };
	DragMode dragMode = DragMode::None;
	double dragStartX = 0, dragStartY = 0;
	double startLeft = 0, startTop = 0, startW = 0, startH = 0;
	struct vec2 startPos = {};

	obs_display_t *display = nullptr;
	obs_weak_source_t *selected = nullptr; /* scene item's parent scene source */
	int64_t selectedId = 0;
};
