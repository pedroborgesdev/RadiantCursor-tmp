import { app } from "electron";

import { KWinController } from "./kwin/kwin-adapter";
import { sharedDataRoot } from "./paths";
import type { RuntimeAdapter } from "./runtime-adapter";
import { UnsupportedRuntimeAdapter } from "./unsupported-adapter";
import { WindowsRuntimeAdapter } from "./windows/windows-adapter";

export function createRuntimeAdapter(): RuntimeAdapter {
  if (process.platform === "linux") return new KWinController();
  if (process.platform === "win32") {
    return new WindowsRuntimeAdapter({
      dataRoot: sharedDataRoot(),
      resourcesPath: process.resourcesPath,
      appPath: app.getAppPath(),
      packaged: app.isPackaged,
      setLoginItem: (enabled, executable, arguments_) => app.setLoginItemSettings({
        openAtLogin: enabled,
        path: executable,
        args: arguments_,
        name: "RadiantCursor Runtime",
      }),
    });
  }
  return new UnsupportedRuntimeAdapter();
}
