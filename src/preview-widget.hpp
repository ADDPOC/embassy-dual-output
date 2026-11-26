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

protected:
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	bool event(QEvent *event) override;

private:
	void CreateDisplay();
	void DestroyDisplay();
	static void Draw(void *param, uint32_t cx, uint32_t cy);

	obs_display_t *display = nullptr;
	obs_weak_source_t *selected = nullptr; /* scene item's parent scene source */
	int64_t selectedId = 0;
};
