#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

class EventBus {
public:
    template<typename T>
    void subscribe(std::function<void(const T&)> handler) {
        auto wrapper = [h = std::move(handler)](const void* evt) {
            h(*static_cast<const T*>(evt));
        };
        m_handlers[std::type_index(typeid(T))].push_back(std::move(wrapper));
    }

    template<typename T>
    void publish(const T& event) {
        auto it = m_handlers.find(std::type_index(typeid(T)));
        if (it == m_handlers.end()) return;
        for (auto& handler : it->second)
            handler(&event);
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const void*)>>> m_handlers;
};

EventBus& bus();

#endif // EVENT_BUS_H
