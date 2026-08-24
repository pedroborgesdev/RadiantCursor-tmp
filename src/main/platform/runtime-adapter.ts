import type {
  RadiantCursorSettings,
  RadiantCursorState,
  RuntimeCompatibility,
} from "../../shared/types";

export interface ActiveEngineRevision {
  effectId: string | null;
  revision: string | null;
}

/** Boundary between Electron and the desktop compositor/runtime. */
export interface RuntimeAdapter {
  getState(): Promise<RadiantCursorState>;
  checkCompatibility(): Promise<RuntimeCompatibility>;
  applySettings(settings: unknown): Promise<RadiantCursorState>;
  activateEffect(settings?: unknown): Promise<RadiantCursorState>;
  disableEffect(): Promise<RadiantCursorState>;
  readActiveEngineRevision(): Promise<ActiveEngineRevision>;
  activateEngineRevision(effectId: string, revision: string): Promise<void>;
  dispose?(): Promise<void>;
}

export interface RuntimeStateFile {
  schemaVersion: 1;
  enabled: boolean;
  settings: RadiantCursorSettings;
  activeEffectId: string | null;
  activeRevision: string | null;
  updatedAt: string;
}
