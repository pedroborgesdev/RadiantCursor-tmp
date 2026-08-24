export type ClickEffectStyle =
  | "ripple"
  | "pulse"
  | "target"
  | "burst"
  | "spark"
  | "focus"
  | "halo"
  | "shockwave"
  | "orbit"
  | "petals"
  | "diamond"
  | "sonar"
  | "vortex"
  | "cross"
  | "confetti"
  | "lightning"
  | "bubbles"
  | "heart"
  | "ink"
  | "splash"
  | "nova"
  | "comet"
  | "eclipse"
  | "plasma"
  | "pixelburst"
  | "prism"
  | "flower"
  | "meteor";

export type ClickTrigger = "press" | "release" | "both";

export type TrailStyle =
  | "dots"
  | "soft"
  | "neon"
  | "cometTrail"
  | "smoke"
  | "sparks"
  | "bubbleTrail"
  | "stars"
  | "hearts"
  | "squares"
  | "diamonds"
  | "triangles"
  | "ribbon"
  | "laser"
  | "fire"
  | "ice"
  | "petalTrail"
  | "pixels"
  | "orbitTrail"
  | "rainbow";

export interface RadiantCursorSettings {
  ClickEnabled: boolean;
  Color1: string;
  Color2: string;
  Color3: string;
  LineWidth: number;
  RingLife: number;
  RingSize: number;
  RingCount: number;
  ShowText: boolean;
  Font: string;
  Style: ClickEffectStyle;
  Trigger: ClickTrigger;
  Glow: boolean;
  TrailEnabled: boolean;
  TrailStyle: TrailStyle;
  TrailColor: string;
  TrailSize: number;
  TrailLife: number;
  TrailDensity: number;
  TrailFrequency: number;
  TrailOpacity: number;
  TrailOffsetX: number;
  TrailOffsetY: number;
  TrailDistance: number;
  TrailGlow: boolean;
  TrailOnlyPressed: boolean;
}

export type RuntimePlatform = "kwin" | "windows" | "unsupported";

export interface RuntimeCompatibility {
  platform: RuntimePlatform;
  compatible: boolean;
  runtimeInstalled: boolean;
  transportAvailable: boolean;
  /** Linux/KWin compatibility details retained for diagnostics. */
  configReadable?: boolean;
  configWritable?: boolean;
  dbusAvailable?: boolean;
  effectInstalled?: boolean;
  /** Whether the running KWin process discovered the plugin factory. */
  effectDiscovered?: boolean;
  details: string[];
}

/** @deprecated Use RuntimeCompatibility. */
export type KWinCompatibility = RuntimeCompatibility;

export interface RadiantCursorState {
  settings: RadiantCursorSettings;
  /** Whether KWin currently reports the RadiantCursor plugin as loaded. */
  isLoaded: boolean;
  compatibility: RuntimeCompatibility;
  /** Backup made before this process's first write, if kwinrc existed. */
  backupPath: string | null;
}

export interface RadiantCursorApi {
  minimizeWindow(): Promise<void>;
  closeWindow(): Promise<void>;
  getState(): Promise<RadiantCursorState>;
  applySettings(settings: RadiantCursorSettings): Promise<RadiantCursorState>;
  activateEffect(settings?: RadiantCursorSettings): Promise<RadiantCursorState>;
  disableEffect(): Promise<RadiantCursorState>;
  checkCompatibility(): Promise<RuntimeCompatibility>;
  listEffects(): Promise<import("./motion/schema").EffectSummary[]>;
  loadEffect(id: string): Promise<import("./motion/schema").EffectDocumentV2>;
  saveDraft(document: import("./motion/schema").EffectDocumentV2): Promise<import("./motion/schema").SaveResult>;
  deployEffect(document: import("./motion/schema").EffectDocumentV2): Promise<import("./motion/schema").DeployResult>;
  deleteEffect(id: string): Promise<void>;
  getRuntimeStatus(): Promise<import("./engine/schema").RuntimeStatus>;
  importImage(): Promise<import("./engine/schema").AssetRecord | null>;
  getAssetPreview(assetId: string): Promise<string | null>;
  exportEffect(id: string): Promise<string | null>;
  importEffect(): Promise<import("./motion/schema").ImportResult | null>;
  onStateChanged(listener: (state: RadiantCursorState) => void): () => void;
}

declare global {
  interface Window {
    readonly radiantcursor: RadiantCursorApi;
  }
}

export const DEFAULT_RADIANT_CURSOR_SETTINGS: Readonly<RadiantCursorSettings> = Object.freeze({
  ClickEnabled: true,
  Color1: "#ff0000",
  Color2: "#00ff00",
  Color3: "#0000ff",
  LineWidth: 1,
  RingLife: 300,
  RingSize: 20,
  RingCount: 2,
  ShowText: true,
  Font: "Noto Sans,10,-1,5,50,0,0,0,0,0",
  Style: "ripple",
  Trigger: "press",
  Glow: true,
  TrailEnabled: false,
  TrailStyle: "dots",
  TrailColor: "#ffffff",
  TrailSize: 14,
  TrailLife: 520,
  TrailDensity: 65,
  TrailFrequency: 30,
  TrailOpacity: 0.72,
  TrailOffsetX: 8,
  TrailOffsetY: 8,
  TrailDistance: 0,
  TrailGlow: true,
  TrailOnlyPressed: false,
});

export const RADIANT_CURSOR_IPC = Object.freeze({
  windowMinimize: "radiantcursor:window-minimize",
  windowClose: "radiantcursor:window-close",
  getState: "radiantcursor:get-state",
  applySettings: "radiantcursor:apply-settings",
  activateEffect: "radiantcursor:activate-effect",
  disableEffect: "radiantcursor:disable-effect",
  checkCompatibility: "radiantcursor:check-compatibility",
  listEffects: "radiantcursor:list-effects",
  loadEffect: "radiantcursor:load-effect",
  saveDraft: "radiantcursor:save-draft",
  deployEffect: "radiantcursor:deploy-effect",
  deleteEffect: "radiantcursor:delete-effect",
  getRuntimeStatus: "radiantcursor:get-runtime-status",
  importImage: "radiantcursor:import-image",
  getAssetPreview: "radiantcursor:get-asset-preview",
  exportEffect: "radiantcursor:export-effect",
  importEffect: "radiantcursor:import-effect",
  stateChanged: "radiantcursor:state-changed",
});
