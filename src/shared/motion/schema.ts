export type Vec2 = readonly [number, number];
export type ColorHex = `#${string}`;
export type EffectTarget = "click" | "halo";

export type EasingName =
  | "linear" | "easeInQuad" | "easeOutQuad" | "easeInOutQuad"
  | "easeInCubic" | "easeOutCubic" | "easeInOutCubic"
  | "easeOutBack" | "easeOutBounce" | "easeOutElastic";

export type ShapeKind = "circle" | "rectangle" | "triangle" | "diamond" | "star" | "hexagon" | "polygon" | "line";
export type AnimationProperty = "position.x" | "position.y" | "rotation" | "scale.x" | "scale.y" | "opacity";
export type AnimationComposition = "add" | "multiply";
export type AnimationPreset =
  | "fadeIn" | "fadeOut" | "scaleIn" | "scaleOut" | "slideIn" | "slideOut"
  | "dropIn" | "dropOut" | "popIn" | "shrinkOut" | "rotateIn" | "rotateOut"
  | "move" | "moveAway" | "moveToward" | "fall" | "rise" | "orbit" | "shake" | "bounce"
  | "scale" | "rotate" | "stretch" | "squash" | "pulse" | "spin";

export interface TimingDocument { startMs: number; durationMs: number }
export interface TransformDocument {
  position: Vec2;
  size: Vec2;
  scale: Vec2;
  rotationDeg: number;
  anchor: Vec2;
  skewXDeg: number;
}
export interface FillDocument { enabled: boolean; color: ColorHex }
export interface StrokeDocument { enabled: boolean; color: ColorHex; width: number }
export interface AppearanceDocument { fill: FillDocument; stroke: StrokeDocument; opacity: number; blendMode: "normal" | "additive" }

export interface AnimationKeyframe { time: number; value: number; easing?: EasingName }
export interface AnimationTrack {
  property: AnimationProperty;
  composition: AnimationComposition;
  keyframes: AnimationKeyframe[];
}
interface AnimationBase { id: string; name: string; enabled: boolean; timing: TimingDocument; easing: EasingName }
export interface PresetAnimationDocument extends AnimationBase {
  type: "preset";
  preset: AnimationPreset;
  params: { amount?: number; distance?: number; angleDeg?: number; turns?: number; x?: number; y?: number };
}
export interface TrackAnimationDocument extends AnimationBase { type: "tracks"; tracks: AnimationTrack[] }
export type AnimationDocument = PresetAnimationDocument | TrackAnimationDocument;

interface BaseElementDocument {
  id: string;
  type: "shape" | "group";
  name: string;
  visible: boolean;
  locked: boolean;
  timing: TimingDocument;
  transform: TransformDocument;
  opacity: number;
  animations: AnimationDocument[];
}
export interface ShapeElementDocument extends BaseElementDocument {
  type: "shape";
  shape: { kind: ShapeKind; sides: number; points: number; innerRatio: number };
  appearance: AppearanceDocument;
}
export interface GroupElementDocument extends BaseElementDocument { type: "group"; children: string[] }
export type ElementDocument = ShapeElementDocument | GroupElementDocument;

export interface EffectDocumentV2 {
  schemaVersion: 2;
  target?: EffectTarget;
  id: string;
  revision?: `sha256:${string}`;
  metadata: { name: string; description: string; author: string; tags: string[]; favorite?: boolean };
  durationMs: number;
  rootIds: string[];
  elements: Record<string, ElementDocument>;
}

export interface EditorState {
  selectedIds: string[];
  primaryId: string | null;
  expandedIds: string[];
  playheadMs: number;
  playing: boolean;
  viewport: { zoom: number; pan: Vec2 };
  timeline: { zoom: number; scrollMs: number };
  snapToGrid: boolean;
  snapToElements: boolean;
  gridSize: number;
}

export interface RuntimeKeyframe { time: number; value: number; easing: EasingName }
export interface RuntimeChannel { property: AnimationProperty; composition: AnimationComposition; keyframes: RuntimeKeyframe[] }
export interface RuntimeNode {
  id: string;
  name: string;
  kind: "group" | "shape";
  parentIndex: number;
  subtreeEnd: number;
  visible: boolean;
  startMs: number;
  durationMs: number;
  transform: TransformDocument;
  opacity: number;
  channels: RuntimeChannel[];
  shape?: ShapeElementDocument["shape"];
  appearance?: AppearanceDocument;
}
export interface RuntimeDefinition {
  runtimeVersion: 1;
  compilerVersion: 1;
  effectId: string;
  target: EffectTarget;
  revision?: `sha256:${string}`;
  durationMs: number;
  maxRadius: number;
  nodes: RuntimeNode[];
}

export interface EngineDiagnostic { severity: "error" | "warning"; code: string; path: string; message: string }
export interface ValidationResult { valid: boolean; diagnostics: EngineDiagnostic[]; document?: EffectDocumentV2 }
export interface EffectSummary { id: string; name: string; description: string; revision: string | null; updatedAt: string; layerCount: number; favorite: boolean; builtIn: boolean; target: EffectTarget }
export interface SaveResult { document: EffectDocumentV2; savedAt: string }
export interface ImportResult { document: EffectDocumentV2; importedAssets: number; sourcePath: string }
export interface DeployResult { effectId: string; revision: string; target: EffectTarget; deployedAt: string; diagnostics: EngineDiagnostic[] }

export const DEFAULT_TRANSFORM: TransformDocument = { position: [0, 0], size: [64, 64], scale: [1, 1], rotationDeg: 0, anchor: [.5, .5], skewXDeg: 0 };
