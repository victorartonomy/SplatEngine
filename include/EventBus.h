#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

// EventBus — synchronous, type-safe publish/subscribe event dispatcher.
//
// Usage:
//   bus().subscribe<MyEvent>([](const MyEvent& e) { ... });
//   bus().publish(MyEvent{...});
//
// How it works:
//   Handlers are stored as type-erased std::function<void(const void*)> in a
//   map keyed by std::type_index. On publish<T>, the concrete T* is cast to
//   void* and each registered wrapper re-casts it back to const T& before
//   calling the user handler. This avoids a virtual dispatch per handler while
//   remaining fully type-safe at the call sites.
//
//   Dispatch is synchronous: publish() calls all handlers inline before returning.
//   There is no queue, no threading, and no re-entrancy guard.
class EventBus {
public:
    // Register a handler to be called whenever an event of type T is published.
    // Multiple handlers per type are allowed; they are called in registration order.
    template<typename T>
    void subscribe(std::function<void(const T&)> handler) {
        // Wrap the typed handler in a type-erased lambda that accepts void*.
        // The void* is always a valid const T* because publish<T> passes &event.
        auto wrapper = [h = std::move(handler)](const void* evt) {
            h(*static_cast<const T*>(evt));
        };
        m_handlers[std::type_index(typeid(T))].push_back(std::move(wrapper));
    }

    // Dispatch an event to all subscribers registered for type T.
    // Calls every handler synchronously before returning.
    template<typename T>
    void publish(const T& event) {
        auto it = m_handlers.find(std::type_index(typeid(T)));
        if (it == m_handlers.end()) return; // No subscribers — nothing to do
        for (auto& handler : it->second)
            handler(&event);
    }

private:
    // Map from event type → list of type-erased handlers for that type
    std::unordered_map<std::type_index, std::vector<std::function<void(const void*)>>> m_handlers;
};

// Global singleton accessor. Returns the one shared EventBus for the process.
// Defined in EventBus.cpp as a function-local static (thread-safe in C++11+).
EventBus& bus();

#endif // EVENT_BUS_H
