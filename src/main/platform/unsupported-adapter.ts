import { DEFAULT_RADIANT_CURSOR_SETTINGS, type RadiantCursorState, type RuntimeCompatibility } from "../../shared/types";
import type { ActiveEngineRevision, RuntimeAdapter } from "./runtime-adapter";

const compatibility: RuntimeCompatibility = {
  platform: "unsupported",
  compatible: false,
  runtimeInstalled: false,
  transportAvailable: false,
  details: ["Este sistema operacional ainda não possui um runtime do RadiantCursor."],
};

export class UnsupportedRuntimeAdapter implements RuntimeAdapter {
  private fail(): never { throw new Error(compatibility.details[0]); }
  async checkCompatibility(): Promise<RuntimeCompatibility> { return compatibility; }
  async getState(): Promise<RadiantCursorState> { return { settings: { ...DEFAULT_RADIANT_CURSOR_SETTINGS }, isLoaded: false, compatibility, backupPath: null }; }
  async applySettings(): Promise<RadiantCursorState> { return this.fail(); }
  async activateEffect(): Promise<RadiantCursorState> { return this.fail(); }
  async disableEffect(): Promise<RadiantCursorState> { return this.getState(); }
  async readActiveEngineRevision(): Promise<ActiveEngineRevision> { return { effectId: null, revision: null }; }
  async activateEngineRevision(): Promise<void> { this.fail(); }
}
