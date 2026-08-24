import { homedir } from "node:os";
import { isAbsolute, join, resolve } from "node:path";

export function sharedDataRoot(): string {
  if (process.platform === "win32") {
    const localAppData = process.env.LOCALAPPDATA;
    const parent = localAppData && isAbsolute(localAppData)
      ? resolve(localAppData)
      : join(homedir(), "AppData", "Local");
    return join(parent, "RadiantCursor");
  }

  const dataHome = process.env.XDG_DATA_HOME;
  const parent = dataHome && isAbsolute(dataHome)
    ? resolve(dataHome)
    : join(homedir(), ".local", "share");
  // Keep the existing Linux location so upgrades do not orphan projects.
  return join(parent, "radiantcursor-studio");
}

export function sharedConfigRoot(): string {
  if (process.platform === "win32") return sharedDataRoot();
  const configHome = process.env.XDG_CONFIG_HOME;
  const parent = configHome && isAbsolute(configHome)
    ? resolve(configHome)
    : join(homedir(), ".config");
  return join(parent, "radiantcursor-studio");
}
