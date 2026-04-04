#ifndef EVENTS_H
#define EVENTS_H

#include <string>

#include "AssetHandle.h"
#include "InputManager.h"

struct WindowResizeEvent {
    int width;
    int height;
};

struct AssetLoadedEvent {
    AssetHandle handle;
    std::string path;
};

struct ActionEvent {
    Action action;
    bool   pressed;
};

#endif // EVENTS_H
