import type { EngineCapability } from "./limits";

export type Vec2 = readonly [number, number];
export type ColorHex = `#${string}`;
export type EasingName =
  | "linear"
  | "easeInQuad"
  | "easeOutQuad"
  | "easeInOutQuad"
  | "easeInCubic"
  | "easeOutCubic"
  | "easeInOutCubic"
  | "easeOutBack";

export interface Keyframe<T> {
  time: number;
  value: T;
  easing?: EasingName;
}

export type Animatable<T> = T | { keyframes: Keyframe<T>[] };

export type ColorContextRef =
  | { ref: "click.buttonColor" }
  | { ref: "click.primaryColor" }
  | { ref: "click.secondaryColor" };

export type ImageContextRef = { ref: "click.buttonImage" };
export type ColorValue = ColorHex | ColorContextRef;
export type AssetValue =
  | ImageContextRef
  | { assetId: `sha256:${string}`; mediaType: "image/png" | "image/jpeg" | "image/webp" };

export interface LayerTiming {
  startMs: number;
  durationMs: number;
}

export interface TransformDocument {
  position: Animatable<Vec2>;
  anchor: Vec2;
  scale: Animatable<Vec2>;
  rotationDeg: Animatable<number>;
}

export interface StrokeDocument {
  color: ColorValue;
  width: Animatable<number>;
}

export interface FillDocument {
  color: ColorValue;
}

export type BlendMode = "normal" | "additive";

export interface MaterialDocument {
  fill: FillDocument | null;
  stroke: StrokeDocument | null;
  opacity: Animatable<number>;
  blendMode: BlendMode;
  glow: number;
}

export type ShapeGeometry =
  | { kind: "circle"; radius: number }
  | { kind: "ring"; radius: number }
  | { kind: "rect"; size: Vec2; cornerRadius: number }
  | { kind: "line"; start: Vec2; end: Vec2 }
  | { kind: "polygon"; radius: number; sides: number }
  | { kind: "star"; outerRadius: number; innerRadius: number; points: number }
  | { kind: "diamond"; size: Vec2 };

export interface BaseLayerDocument {
  id: string;
  name: string;
  enabled: boolean;
  timing: LayerTiming;
  transform: TransformDocument;
}

export interface ShapeLayerDocument extends BaseLayerDocument {
  type: "shape";
  geometry: ShapeGeometry;
  material: MaterialDocument;
}

export type ParticleDistribution = "even" | "random";
export type EmitterMode = "radial" | "cone" | "point";

export interface ParticleEmitterDocument {
  mode: EmitterMode;
  count: number;
  distribution: ParticleDistribution;
  angleDeg: number;
  spreadDeg: number;
  speed: number;
  speedVariation: number;
  spawnRadius: number;
  gravity: Vec2;
  drag: number;
  variants: {
    count: number;
    avoidImmediateRepeat: boolean;
  };
}

export interface ParticleTemplateDocument {
  geometry: ShapeGeometry;
  material: MaterialDocument;
  scale: Animatable<number>;
  rotationDeg: Animatable<number>;
}

export interface ParticleLayerDocument extends BaseLayerDocument {
  type: "particles";
  emitter: ParticleEmitterDocument;
  particle: ParticleTemplateDocument;
}

export interface ImageLayerDocument extends BaseLayerDocument {
  type: "image";
  asset: AssetValue;
  size: Vec2;
  material: Pick<MaterialDocument, "opacity" | "blendMode" | "glow">;
}

export type LayerDocument =
  | ShapeLayerDocument
  | ParticleLayerDocument
  | ImageLayerDocument;

export interface EffectMetadata {
  name: string;
  description: string;
  author: string;
  tags: string[];
  favorite?: boolean;
}

export interface EffectDocument {
  schemaVersion: 1;
  engine: {
    minimumVersion: number;
    requiredCapabilities: EngineCapability[];
  };
  id: string;
  revision?: `sha256:${string}`;
  metadata: EffectMetadata;
  durationMs: number;
  layers: LayerDocument[];
}

export interface EffectEditorState {
  selectedLayerIds: string[];
  playheadMs: number;
  playing: boolean;
  zoom: number;
  dirty: boolean;
}

export interface EffectSummary {
  id: string;
  name: string;
  description: string;
  revision: string | null;
  updatedAt: string;
  layerCount: number;
  favorite: boolean;
  builtIn: boolean;
}

export interface EngineDiagnostic {
  severity: "error" | "warning";
  code: string;
  path: string;
  message: string;
}

export interface ValidationResult {
  valid: boolean;
  diagnostics: EngineDiagnostic[];
  document?: EffectDocument;
}

export interface SaveResult {
  document: EffectDocument;
  savedAt: string;
}

export interface AssetRecord {
  assetId: `sha256:${string}`;
  mediaType: "image/png" | "image/jpeg" | "image/webp";
  width: number;
  height: number;
  bytes: number;
}

export interface ImportResult {
  document: EffectDocument;
  importedAssets: number;
  sourcePath: string;
}

export interface DeployResult {
  effectId: string;
  revision: string;
  deployedAt: string;
  diagnostics: EngineDiagnostic[];
}

export interface RuntimeStatus {
  engineVersion: number;
  activeEffectId: string | null;
  activeRevision: string | null;
  activeHaloEffectId?: string | null;
  activeHaloRevision?: string | null;
  lastKnownGoodRevision: string | null;
  diagnostics: EngineDiagnostic[];
  capabilities: readonly string[];
}

export const DEFAULT_TRANSFORM: TransformDocument = {
  position: [0, 0],
  anchor: [0.5, 0.5],
  scale: [1, 1],
  rotationDeg: 0,
};

export const BUTTON_COLOR_REF: ColorContextRef = { ref: "click.buttonColor" };
