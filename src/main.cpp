#include <memory>

#include "threadlock_app.h"
#include "wlw/app/entry_point.h"

// The app: the engine's services + Threadlock as its one system.
std::unique_ptr<wlw::app::Application> MakeApp() {
  auto app = std::make_unique<wlw::app::Application>(threadlock::ThreadlockApp::MakeConfig());
  app->AddSystem<threadlock::ThreadlockApp>();
  return app;
}

// Defines main() on desktop and android_main() on Android.
WLW_MAIN(MakeApp)
