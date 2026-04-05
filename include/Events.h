#ifndef EVENTS_H
#define EVENTS_H

#include <string>

#include "AssetHandle.h"
#include "InputManager.h"

// -----------------------------------------------------------------------
// Event structs — plain data bags published through the global EventBus.
// Any system can subscribe to these without coupling to the publisher.
// -----------------------------------------------------------------------

// Published by Application::update() when the ImGui viewport panel changes size.
// Subscriber: Renderer (via bus().subscribe in Application::initialize()) calls resize().
struct WindowResizeEvent {
    int width;   // New viewport width in pixels
    int height;  // New viewport height in pixels
};

// Published by AssetManager after a mesh has been fully uploaded to the GPU.
// Fired from both the synchronous load() path and the async update() path.
struct AssetLoadedEvent {
    AssetHandle handle; // Handle that can now be passed to AssetManager::get()
    std::string path;   // Original file path, for logging / UI display
};

// Published by InputManager::update() whenever a named Action changes state
// (pressed or released). Only fires on the edge (rising or falling), not every frame.
struct ActionEvent {
    Action action;  // Which named action changed (e.g., Action::Quit, Action::CameraFwd)
    bool   pressed; // true = just pressed (rising edge), false = just released (falling edge)
};

#endif // EVENTS_H
