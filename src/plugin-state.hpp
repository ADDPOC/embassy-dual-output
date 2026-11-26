#pragma once

#include "stream-group.hpp"

/* Two stream groups: portrait canvas and extra landscape destinations */
StreamGroup &PortraitGroup();
StreamGroup &LandscapeGroup();

/* Writes stream settings (destinations, bitrates) to the plugin config file */
void SaveStreamSettings();
