#include "EventBus.h"

// Global EventBus singleton accessor.
//
// The function-local static is guaranteed to be initialized exactly once the first
// time bus() is called, and destroyed when the program exits. This is thread-safe
// per the C++11 standard (magic statics), so bus() can be called safely from any
// thread — though the EventBus itself dispatches synchronously and is not thread-safe.
//
// Using a function-local static avoids the "static initialization order fiasco":
// any code that calls bus() (including other static initializers) will always get
// a fully constructed EventBus rather than a partially initialized global object.
EventBus& bus() {
    static EventBus instance;
    return instance;
}
