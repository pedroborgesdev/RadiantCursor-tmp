import { ENGINE_CAPABILITIES, ENGINE_LIMITS, ENGINE_VERSION } from "./limits";
import type {
  Animatable,
  AssetValue,
  BlendMode,
  ColorValue,
  EasingName,
  EffectDocument,
  EngineDiagnostic,
  FillDocument,
  ImageLayerDocument,
  LayerDocument,
  MaterialDocument,
  ParticleLayerDocument,
  ShapeGeometry,
  ShapeLayerDocument,
  StrokeDocument,
  TransformDocument,
  ValidationResult,
  Vec2,
} from "./schema";

const EASINGS = new Set<EasingName>([
  "linear", "easeInQuad", "easeOutQuad", "easeInOutQuad", "easeInCubic",
  "easeOutCubic", "easeInOutCubic", "easeOutBack",
]);
const CAPABILITIES = new Set<string>(ENGINE_CAPABILITIES);
const ID_PATTERN = /^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$/;
const HASH_PATTERN = /^sha256:[a-f0-9]{64}$/;
const COLOR_PATTERN = /^#[a-fA-F0-9]{6}(?:[a-fA-F0-9]{2})?$/;

type JsonRecord = Record<string, unknown>;

class ValidationContext {
  readonly diagnostics: EngineDiagnostic[] = [];

  error(code: string, path: string, message: string): void {
    this.diagnostics.push({ severity: "error", code, path, message });
  }

  warning(code: string, path: string, message: string): void {
    this.diagnostics.push({ severity: "warning", code, path, message });
  }
}

function record(value: unknown): value is JsonRecord {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function stringValue(ctx: ValidationContext, value: unknown, path: string, max: number): string {
  if (typeof value !== "string" || value.length === 0 || value.length > max || /[\u0000-\u001f\u007f]/.test(value)) {
    ctx.error("invalid_string", path, `Deve ser um texto entre 1 e ${max} caracteres.`);
    return "Inválido";
  }
  return value;
}

function numberValue(
  ctx: ValidationContext,
  value: unknown,
  path: string,
  min: number,
  max: number,
  integer = false,
): number {
  if (typeof value !== "number" || !Number.isFinite(value) || value < min || value > max || (integer && !Number.isInteger(value))) {
    ctx.error("invalid_number", path, `Deve ser ${integer ? "um inteiro" : "um número"} entre ${min} e ${max}.`);
    return min;
  }
  return value;
}

function booleanValue(ctx: ValidationContext, value: unknown, path: string): boolean {
  if (typeof value !== "boolean") {
    ctx.error("invalid_boolean", path, "Deve ser verdadeiro ou falso.");
    return false;
  }
  return value;
}

function enumValue<T extends string>(
  ctx: ValidationContext,
  value: unknown,
  path: string,
  choices: readonly T[],
): T {
  if (typeof value !== "string" || !choices.includes(value as T)) {
    ctx.error("invalid_enum", path, `Opção inválida. Use: ${choices.join(", ")}.`);
    return choices[0]!;
  }
  return value as T;
}

function vec2(ctx: ValidationContext, value: unknown, path: string, min = -10_000, max = 10_000): Vec2 {
  if (!Array.isArray(value) || value.length !== 2) {
    ctx.error("invalid_vec2", path, "Deve ser um vetor [x, y].");
    return [0, 0];
  }
  return [
    numberValue(ctx, value[0], `${path}[0]`, min, max),
    numberValue(ctx, value[1], `${path}[1]`, min, max),
  ];
}

function animatable<T>(
  ctx: ValidationContext,
  value: unknown,
  path: string,
  parse: (value: unknown, path: string) => T,
): Animatable<T> {
  if (!record(value) || !("keyframes" in value)) return parse(value, path);
  if (!Array.isArray(value.keyframes) || value.keyframes.length < 1 || value.keyframes.length > ENGINE_LIMITS.maxKeyframesPerTrack) {
    ctx.error("invalid_keyframes", `${path}.keyframes`, `Use entre 1 e ${ENGINE_LIMITS.maxKeyframesPerTrack} keyframes.`);
    return { keyframes: [{ time: 0, value: parse(undefined, `${path}.keyframes[0].value`) }] };
  }
  let previousTime = -1;
  return {
    keyframes: value.keyframes.map((entry, index) => {
      const framePath = `${path}.keyframes[${index}]`;
      if (!record(entry)) {
        ctx.error("invalid_keyframe", framePath, "Keyframe inválido.");
        return { time: 0, value: parse(undefined, `${framePath}.value`) };
      }
      const time = numberValue(ctx, entry.time, `${framePath}.time`, 0, 1);
      if (time < previousTime) ctx.error("keyframes_unsorted", `${framePath}.time`, "Os keyframes devem estar em ordem crescente.");
      previousTime = time;
      const easing = entry.easing === undefined
        ? undefined
        : enumValue(ctx, entry.easing, `${framePath}.easing`, [...EASINGS]);
      return { time, value: parse(entry.value, `${framePath}.value`), ...(easing ? { easing } : {}) };
    }),
  };
}

function color(ctx: ValidationContext, value: unknown, path: string): ColorValue {
  if (typeof value === "string") {
    if (!COLOR_PATTERN.test(value)) ctx.error("invalid_color", path, "Use #RRGGBB ou #RRGGBBAA.");
    return value as ColorValue;
  }
  if (record(value) && ["click.buttonColor", "click.primaryColor", "click.secondaryColor"].includes(String(value.ref))) {
    return { ref: value.ref as "click.buttonColor" };
  }
  ctx.error("invalid_color", path, "Cor ou referência de contexto inválida.");
  return "#ffffff";
}

function stroke(ctx: ValidationContext, value: unknown, path: string): StrokeDocument | null {
  if (value === null) return null;
  if (!record(value)) {
    ctx.error("invalid_stroke", path, "Contorno inválido.");
    return null;
  }
  return {
    color: color(ctx, value.color, `${path}.color`),
    width: animatable(ctx, value.width, `${path}.width`, (input, inputPath) => numberValue(ctx, input, inputPath, 0, 128)),
  };
}

function fill(ctx: ValidationContext, value: unknown, path: string): FillDocument | null {
  if (value === null) return null;
  if (!record(value)) {
    ctx.error("invalid_fill", path, "Preenchimento inválido.");
    return null;
  }
  return { color: color(ctx, value.color, `${path}.color`) };
}

function material(ctx: ValidationContext, value: unknown, path: string): MaterialDocument {
  if (!record(value)) {
    ctx.error("invalid_material", path, "Material inválido.");
    value = {};
  }
  const source = value as JsonRecord;
  return {
    fill: fill(ctx, source.fill ?? null, `${path}.fill`),
    stroke: stroke(ctx, source.stroke ?? null, `${path}.stroke`),
    opacity: animatable(ctx, source.opacity ?? 1, `${path}.opacity`, (input, inputPath) => numberValue(ctx, input, inputPath, 0, 1)),
    blendMode: enumValue(ctx, source.blendMode ?? "normal", `${path}.blendMode`, ["normal", "additive"] satisfies BlendMode[]),
    glow: numberValue(ctx, source.glow ?? 0, `${path}.glow`, 0, 64),
  };
}

function geometry(ctx: ValidationContext, value: unknown, path: string): ShapeGeometry {
  if (!record(value)) {
    ctx.error("invalid_geometry", path, "Geometria inválida.");
    return { kind: "circle", radius: 20 };
  }
  const kind = enumValue(ctx, value.kind, `${path}.kind`, ["circle", "ring", "rect", "line", "polygon", "star", "diamond"] as const);
  switch (kind) {
    case "circle":
    case "ring": return { kind, radius: numberValue(ctx, value.radius, `${path}.radius`, 0.1, 2_000) };
    case "rect": return { kind, size: vec2(ctx, value.size, `${path}.size`, 0.1, 4_000), cornerRadius: numberValue(ctx, value.cornerRadius ?? 0, `${path}.cornerRadius`, 0, 2_000) };
    case "line": return { kind, start: vec2(ctx, value.start, `${path}.start`), end: vec2(ctx, value.end, `${path}.end`) };
    case "polygon": return { kind, radius: numberValue(ctx, value.radius, `${path}.radius`, 0.1, 2_000), sides: numberValue(ctx, value.sides, `${path}.sides`, 3, ENGINE_LIMITS.maxPolygonSides, true) };
    case "star": return { kind, outerRadius: numberValue(ctx, value.outerRadius, `${path}.outerRadius`, 0.1, 2_000), innerRadius: numberValue(ctx, value.innerRadius, `${path}.innerRadius`, 0.1, 2_000), points: numberValue(ctx, value.points, `${path}.points`, 2, 32, true) };
    case "diamond": return { kind, size: vec2(ctx, value.size, `${path}.size`, 0.1, 4_000) };
  }
}

function transform(ctx: ValidationContext, value: unknown, path: string): TransformDocument {
  if (!record(value)) {
    ctx.error("invalid_transform", path, "Transformação inválida.");
    value = {};
  }
  const source = value as JsonRecord;
  return {
    position: animatable(ctx, source.position ?? [0, 0], `${path}.position`, (input, inputPath) => vec2(ctx, input, inputPath)),
    anchor: vec2(ctx, source.anchor ?? [0.5, 0.5], `${path}.anchor`, 0, 1),
    scale: animatable(ctx, source.scale ?? [1, 1], `${path}.scale`, (input, inputPath) => vec2(ctx, input, inputPath, -100, 100)),
    rotationDeg: animatable(ctx, source.rotationDeg ?? 0, `${path}.rotationDeg`, (input, inputPath) => numberValue(ctx, input, inputPath, -36_000, 36_000)),
  };
}

function baseLayer(ctx: ValidationContext, value: JsonRecord, path: string) {
  const timing = record(value.timing) ? value.timing : {};
  if (!record(value.timing)) ctx.error("invalid_timing", `${path}.timing`, "Timing inválido.");
  return {
    id: stringValue(ctx, value.id, `${path}.id`, 80),
    name: stringValue(ctx, value.name, `${path}.name`, 80),
    enabled: booleanValue(ctx, value.enabled, `${path}.enabled`),
    timing: {
      startMs: numberValue(ctx, timing.startMs, `${path}.timing.startMs`, 0, ENGINE_LIMITS.maxDurationMs, true),
      durationMs: numberValue(ctx, timing.durationMs, `${path}.timing.durationMs`, 1, ENGINE_LIMITS.maxDurationMs, true),
    },
    transform: transform(ctx, value.transform, `${path}.transform`),
  };
}

function shapeLayer(ctx: ValidationContext, value: JsonRecord, path: string): ShapeLayerDocument {
  return { ...baseLayer(ctx, value, path), type: "shape", geometry: geometry(ctx, value.geometry, `${path}.geometry`), material: material(ctx, value.material, `${path}.material`) };
}

function particleLayer(ctx: ValidationContext, value: JsonRecord, path: string): ParticleLayerDocument {
  const emitter = record(value.emitter) ? value.emitter : {};
  const variants = record(emitter.variants) ? emitter.variants : {};
  const particle = record(value.particle) ? value.particle : {};
  if (!record(value.emitter)) ctx.error("invalid_emitter", `${path}.emitter`, "Emissor inválido.");
  if (!record(value.particle)) ctx.error("invalid_particle", `${path}.particle`, "Template de partícula inválido.");
  return {
    ...baseLayer(ctx, value, path),
    type: "particles",
    emitter: {
      mode: enumValue(ctx, emitter.mode, `${path}.emitter.mode`, ["radial", "cone", "point"] as const),
      count: numberValue(ctx, emitter.count, `${path}.emitter.count`, 1, ENGINE_LIMITS.maxParticlesPerLayer, true),
      distribution: enumValue(ctx, emitter.distribution, `${path}.emitter.distribution`, ["even", "random"] as const),
      angleDeg: numberValue(ctx, emitter.angleDeg ?? 0, `${path}.emitter.angleDeg`, -36_000, 36_000),
      spreadDeg: numberValue(ctx, emitter.spreadDeg ?? 360, `${path}.emitter.spreadDeg`, 0, 360),
      speed: numberValue(ctx, emitter.speed, `${path}.emitter.speed`, 0, 5_000),
      speedVariation: numberValue(ctx, emitter.speedVariation ?? 0, `${path}.emitter.speedVariation`, 0, 5_000),
      spawnRadius: numberValue(ctx, emitter.spawnRadius ?? 0, `${path}.emitter.spawnRadius`, 0, 2_000),
      gravity: vec2(ctx, emitter.gravity ?? [0, 0], `${path}.emitter.gravity`, -5_000, 5_000),
      drag: numberValue(ctx, emitter.drag ?? 0, `${path}.emitter.drag`, 0, 1),
      variants: {
        count: numberValue(ctx, variants.count ?? 1, `${path}.emitter.variants.count`, 1, 16, true),
        avoidImmediateRepeat: booleanValue(ctx, variants.avoidImmediateRepeat ?? true, `${path}.emitter.variants.avoidImmediateRepeat`),
      },
    },
    particle: {
      geometry: geometry(ctx, particle.geometry, `${path}.particle.geometry`),
      material: material(ctx, particle.material, `${path}.particle.material`),
      scale: animatable(ctx, particle.scale ?? 1, `${path}.particle.scale`, (input, inputPath) => numberValue(ctx, input, inputPath, -100, 100)),
      rotationDeg: animatable(ctx, particle.rotationDeg ?? 0, `${path}.particle.rotationDeg`, (input, inputPath) => numberValue(ctx, input, inputPath, -36_000, 36_000)),
    },
  };
}

function asset(ctx: ValidationContext, value: unknown, path: string): AssetValue {
  if (record(value) && value.ref === "click.buttonImage") return { ref: "click.buttonImage" };
  if (record(value) && typeof value.assetId === "string" && HASH_PATTERN.test(value.assetId)) {
    return {
      assetId: value.assetId as `sha256:${string}`,
      mediaType: enumValue(ctx, value.mediaType, `${path}.mediaType`, ["image/png", "image/jpeg", "image/webp"] as const),
    };
  }
  ctx.error("invalid_asset", path, "Asset inválido ou hash SHA-256 malformado.");
  return { ref: "click.buttonImage" };
}

function imageLayer(ctx: ValidationContext, value: JsonRecord, path: string): ImageLayerDocument {
  const parsedMaterial = material(ctx, value.material, `${path}.material`);
  return {
    ...baseLayer(ctx, value, path),
    type: "image",
    asset: asset(ctx, value.asset, `${path}.asset`),
    size: vec2(ctx, value.size, `${path}.size`, 0.1, 4_096),
    material: { opacity: parsedMaterial.opacity, blendMode: parsedMaterial.blendMode, glow: parsedMaterial.glow },
  };
}

function layer(ctx: ValidationContext, value: unknown, path: string): LayerDocument {
  if (!record(value)) {
    ctx.error("invalid_layer", path, "Layer inválida.");
    value = { type: "shape" };
  }
  const source = value as JsonRecord;
  const type = enumValue(ctx, source.type, `${path}.type`, ["shape", "particles", "image"] as const);
  if (type === "particles") return particleLayer(ctx, source, path);
  if (type === "image") return imageLayer(ctx, source, path);
  return shapeLayer(ctx, source, path);
}

export function validateEffectDocument(input: unknown): ValidationResult {
  const ctx = new ValidationContext();
  let serializedSize = 0;
  try { serializedSize = new TextEncoder().encode(JSON.stringify(input)).byteLength; } catch { ctx.error("not_serializable", "$", "Documento não serializável."); }
  if (serializedSize > ENGINE_LIMITS.maxJsonBytes) ctx.error("document_too_large", "$", `Documento excede ${ENGINE_LIMITS.maxJsonBytes} bytes.`);
  if (!record(input)) return { valid: false, diagnostics: [{ severity: "error", code: "invalid_document", path: "$", message: "O efeito deve ser um objeto JSON." }] };

  const schemaVersion = numberValue(ctx, input.schemaVersion, "$.schemaVersion", 1, 1, true);
  const engine = record(input.engine) ? input.engine : {};
  if (!record(input.engine)) ctx.error("invalid_engine", "$.engine", "Requisitos da engine inválidos.");
  const minimumVersion = numberValue(ctx, engine.minimumVersion, "$.engine.minimumVersion", 1, 1_000, true);
  if (minimumVersion > ENGINE_VERSION) ctx.error("engine_too_old", "$.engine.minimumVersion", `Este app suporta a engine até a versão ${ENGINE_VERSION}.`);
  const requiredCapabilities = Array.isArray(engine.requiredCapabilities) ? engine.requiredCapabilities : [];
  if (!Array.isArray(engine.requiredCapabilities)) ctx.error("invalid_capabilities", "$.engine.requiredCapabilities", "Use uma lista de capabilities.");
  const capabilities = requiredCapabilities.map((capability, index) => {
    const parsed = stringValue(ctx, capability, `$.engine.requiredCapabilities[${index}]`, 80);
    if (!CAPABILITIES.has(parsed)) ctx.error("unsupported_capability", `$.engine.requiredCapabilities[${index}]`, `Capability não suportada: ${parsed}.`);
    return parsed as EffectDocument["engine"]["requiredCapabilities"][number];
  });

  const metadata = record(input.metadata) ? input.metadata : {};
  if (!record(input.metadata)) ctx.error("invalid_metadata", "$.metadata", "Metadados inválidos.");
  const tags = Array.isArray(metadata.tags) ? metadata.tags : [];
  if (!Array.isArray(metadata.tags) || tags.length > ENGINE_LIMITS.maxTags) ctx.error("invalid_tags", "$.metadata.tags", `Use no máximo ${ENGINE_LIMITS.maxTags} tags.`);
  const layersInput = Array.isArray(input.layers) ? input.layers : [];
  if (!Array.isArray(input.layers) || layersInput.length < 1 || layersInput.length > ENGINE_LIMITS.maxLayers) ctx.error("invalid_layers", "$.layers", `Use entre 1 e ${ENGINE_LIMITS.maxLayers} layers.`);

  const document: EffectDocument = {
    schemaVersion: schemaVersion as 1,
    engine: { minimumVersion, requiredCapabilities: capabilities },
    id: stringValue(ctx, input.id, "$.id", 80),
    ...(typeof input.revision === "string" && HASH_PATTERN.test(input.revision) ? { revision: input.revision as `sha256:${string}` } : {}),
    metadata: {
      name: stringValue(ctx, metadata.name, "$.metadata.name", ENGINE_LIMITS.maxNameLength),
      description: typeof metadata.description === "string" && metadata.description.length <= ENGINE_LIMITS.maxDescriptionLength ? metadata.description : "",
      author: typeof metadata.author === "string" && metadata.author.length <= ENGINE_LIMITS.maxNameLength ? metadata.author : "",
      tags: tags.slice(0, ENGINE_LIMITS.maxTags).map((tag, index) => stringValue(ctx, tag, `$.metadata.tags[${index}]`, ENGINE_LIMITS.maxTagLength)),
      ...(typeof metadata.favorite === "boolean" ? { favorite: metadata.favorite } : {}),
    },
    durationMs: numberValue(ctx, input.durationMs, "$.durationMs", 1, ENGINE_LIMITS.maxDurationMs, true),
    layers: layersInput.slice(0, ENGINE_LIMITS.maxLayers).map((entry, index) => layer(ctx, entry, `$.layers[${index}]`)),
  };

  if (!ID_PATTERN.test(document.id)) ctx.error("invalid_id", "$.id", "Use letras, números, ponto, hífen ou sublinhado; máximo de 80 caracteres.");
  const ids = new Set<string>();
  for (const [index, parsedLayer] of document.layers.entries()) {
    if (!ID_PATTERN.test(parsedLayer.id)) ctx.error("invalid_layer_id", `$.layers[${index}].id`, "ID de layer inválido.");
    if (ids.has(parsedLayer.id)) ctx.error("duplicate_layer_id", `$.layers[${index}].id`, "IDs de layer precisam ser únicos.");
    ids.add(parsedLayer.id);
    if (parsedLayer.timing.startMs + parsedLayer.timing.durationMs > document.durationMs) ctx.warning("layer_outside_duration", `$.layers[${index}].timing`, "A layer termina depois da duração do efeito e será cortada.");
  }

  const valid = !ctx.diagnostics.some((diagnostic) => diagnostic.severity === "error");
  return { valid, diagnostics: ctx.diagnostics, ...(valid ? { document } : {}) };
}

export class EffectValidationError extends Error {
  constructor(readonly diagnostics: EngineDiagnostic[]) {
    super(diagnostics.find((entry) => entry.severity === "error")?.message ?? "Documento de efeito inválido.");
    this.name = "EffectValidationError";
  }
}

export function assertEffectDocument(input: unknown): EffectDocument {
  const result = validateEffectDocument(input);
  if (!result.valid || !result.document) throw new EffectValidationError(result.diagnostics);
  return result.document;
}
