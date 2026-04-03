#include "Application.h"

int main() {
    Application app;
    if (!app.initialize()) return -1;
    app.run();
    app.shutdown();
    return 0;
}
