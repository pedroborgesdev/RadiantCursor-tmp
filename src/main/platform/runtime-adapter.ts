import type {
  RadiantCursorSettings,
  RadiantCursorState,
  RuntimeCompatibility,
} from "../../shared/types";
import type { EffectTarget } from "../../shared/motion/schema";

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
  readActiveEngineRevision(target?: EffectTarget): Promise<ActiveEngineRevision>;
  activateEngineRevision(effectId: string, revision: string, target?: EffectTarget): Promise<void>;
  dispose?(): Promise<void>;
}

export interface RuntimeStateFile {
  schemaVersion: 1;
  enabled: boolean;
  settings: RadiantCursorSettings;
  activeEffectId: string | null;
  activeRevision: string | null;
  activeHaloEffectId: string | null;
  activeHaloRevision: string | null;
  updatedAt: string;
}
