#include "app.h"

static App s_app;

extern "C" void app_main()
{
    s_app.run();
}
