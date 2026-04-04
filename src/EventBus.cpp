#include "EventBus.h"

EventBus& bus() {
    static EventBus instance;
    return instance;
}
