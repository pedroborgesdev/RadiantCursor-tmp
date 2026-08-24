import { BUTTON_COLOR_REF, DEFAULT_TRANSFORM, type Animatable, type EffectDocument, type LayerDocument, type MaterialDocument } from "./schema";

const fade: Animatable<number> = { keyframes: [{ time: 0, value: 1 }, { time: 0.72, value: 0.85 }, { time: 1, value: 0 }] };

function material(options: Partial<MaterialDocument> = {}): MaterialDocument {
  return {
    fill: null,
    stroke: { color: BUTTON_COLOR_REF, width: 3 },
    opacity: fade,
    blendMode: "normal",
    glow: 8,
    ...options,
  };
}

function base(id: string, name: string, durationMs: number) {
  return {
    id,
    name,
    enabled: true,
    timing: { startMs: 0, durationMs },
    transform: { ...DEFAULT_TRANSFORM },
  };
}

function document(id: string, name: string, description: string, durationMs: number, layers: LayerDocument[]): EffectDocument {
  return {
    schemaVersion: 1,
    engine: {
      minimumVersion: 1,
      requiredCapabilities: ["shape.v1", ...(layers.some((layer) => layer.type === "particles") ? ["particles.v1" as const] : [])],
    },
    id,
    metadata: { name, description, author: "RadiantCursor Studio", tags: ["built-in"] },
    durationMs,
    layers,
  };
}

const ripple = document("builtin.ripple", "Ondas", "Anéis concêntricos com expansão suave.", 620, [
  {
    ...base("ring-back", "Onda secundária", 620),
    type: "shape",
    timing: { startMs: 90, durationMs: 530 },
    geometry: { kind: "ring", radius: 52 },
    transform: { ...DEFAULT_TRANSFORM, scale: { keyframes: [{ time: 0, value: [0.1, 0.1], easing: "easeOutCubic" }, { time: 1, value: [1.25, 1.25] }] } },
    material: material({ stroke: { color: BUTTON_COLOR_REF, width: 2 }, glow: 5 }),
  },
  {
    ...base("ring-main", "Onda principal", 500),
    type: "shape",
    geometry: { kind: "ring", radius: 42 },
    transform: { ...DEFAULT_TRANSFORM, scale: { keyframes: [{ time: 0, value: [0.08, 0.08], easing: "easeOutCubic" }, { time: 1, value: [1, 1] }] } },
    material: material(),
  },
]);

const pulse = document("builtin.pulse", "Pulso", "Disco preenchido com halo de energia.", 520, [
  {
    ...base("pulse-fill", "Núcleo", 430),
    type: "shape",
    geometry: { kind: "circle", radius: 34 },
    transform: { ...DEFAULT_TRANSFORM, scale: { keyframes: [{ time: 0, value: [0.12, 0.12], easing: "easeOutBack" }, { time: 0.5, value: [1, 1] }, { time: 1, value: [1.3, 1.3] }] } },
    material: material({ fill: { color: BUTTON_COLOR_REF }, stroke: null, opacity: { keyframes: [{ time: 0, value: 0.75 }, { time: 1, value: 0 }] }, blendMode: "additive", glow: 14 }),
  },
  {
    ...base("pulse-ring", "Contorno", 520),
    type: "shape",
    geometry: { kind: "ring", radius: 44 },
    transform: { ...DEFAULT_TRANSFORM, scale: { keyframes: [{ time: 0, value: [0.2, 0.2], easing: "easeOutCubic" }, { time: 1, value: [1.2, 1.2] }] } },
    material: material({ stroke: { color: "#ffffff", width: 2 } }),
  },
]);

const target = document("builtin.target", "Alvo", "Mira geométrica que trava sobre o clique.", 650, [
  {
    ...base("target-ring", "Anel", 650),
    type: "shape",
    geometry: { kind: "ring", radius: 38 },
    transform: { ...DEFAULT_TRANSFORM, scale: { keyframes: [{ time: 0, value: [1.6, 1.6], easing: "easeOutCubic" }, { time: 0.38, value: [0.9, 0.9] }, { time: 1, value: [1.1, 1.1] }] } },
    material: material({ glow: 4 }),
  },
  ...([0, 90] as const).map((rotation, index): LayerDocument => ({
    ...base(`target-axis-${index}`, index === 0 ? "Eixo horizontal" : "Eixo vertical", 580),
    type: "shape",
    geometry: { kind: "line", start: [-64, 0], end: [64, 0] },
    transform: { ...DEFAULT_TRANSFORM, rotationDeg: rotation, scale: { keyframes: [{ time: 0, value: [1.4, 1], easing: "easeOutCubic" }, { time: 0.45, value: [0.72, 1] }, { time: 1, value: [0.9, 1] }] } },
    material: material({ stroke: { color: BUTTON_COLOR_REF, width: 2 }, glow: 3 }),
  })),
]);

function particleEffect(
  id: string,
  name: string,
  description: string,
  geometry: Extract<LayerDocument, { type: "particles" }>["particle"]["geometry"],
  options: { count?: number; speed?: number; gravity?: readonly [number, number]; distribution?: "even" | "random" } = {},
): EffectDocument {
  return document(id, name, description, 760, [
    {
      ...base("particles", "Partículas", 760),
      type: "particles",
      emitter: {
        mode: "radial",
        count: options.count ?? 18,
        distribution: options.distribution ?? "random",
        angleDeg: -90,
        spreadDeg: 360,
        speed: options.speed ?? 120,
        speedVariation: 46,
        spawnRadius: 5,
        gravity: options.gravity ?? [0, 36],
        drag: 0.18,
        variants: { count: 4, avoidImmediateRepeat: true },
      },
      particle: {
        geometry,
        material: material({ fill: { color: BUTTON_COLOR_REF }, stroke: null, opacity: fade, blendMode: "additive", glow: 6 }),
        scale: { keyframes: [{ time: 0, value: 0.35, easing: "easeOutBack" }, { time: 0.3, value: 1 }, { time: 1, value: 0.2 }] },
        rotationDeg: { keyframes: [{ time: 0, value: 0 }, { time: 1, value: 220 }] },
      },
    },
  ]);
}

const confetti = particleEffect("builtin.confetti", "Confete", "Fragmentos sólidos em quatro montagens alternadas.", { kind: "rect", size: [8, 4], cornerRadius: 1 }, { count: 24, speed: 150, gravity: [0, 95] });
const bubbles = particleEffect("builtin.bubbles", "Bolhas", "Bolhas leves que sobem e se espalham.", { kind: "ring", radius: 5 }, { count: 15, speed: 80, gravity: [0, -55] });
const pixels = particleEffect("builtin.pixels", "Pixel burst", "Blocos digitais com posições determinísticas variadas.", { kind: "rect", size: [7, 7], cornerRadius: 0 }, { count: 22, speed: 135, gravity: [0, 18] });
const nova = particleEffect("builtin.nova", "Supernova", "Estrelas preenchidas em uma explosão brilhante.", { kind: "star", outerRadius: 7, innerRadius: 3, points: 5 }, { count: 16, speed: 175, gravity: [0, 25], distribution: "even" });

function shapeEffect(
  id: string,
  name: string,
  description: string,
  geometries: Array<Extract<LayerDocument, { type: "shape" }>["geometry"]>,
  filled = false,
  rotation = 0,
): EffectDocument {
  return document(id, name, description, 680, geometries.map((geometry, index) => ({
    ...base(`shape-${index + 1}`, `${name} ${index + 1}`, 680 - index * 35),
    type: "shape",
    timing: { startMs: index * 28, durationMs: 680 - index * 28 },
    geometry,
    transform: {
      ...DEFAULT_TRANSFORM,
      scale: { keyframes: [{ time: 0, value: [0.08, 0.08], easing: "easeOutBack" }, { time: 0.45, value: [1, 1] }, { time: 1, value: [1.24, 1.24] }] },
      rotationDeg: { keyframes: [{ time: 0, value: -rotation }, { time: 1, value: rotation }] },
    },
    material: material(filled && geometry.kind !== "ring"
      ? { fill: { color: BUTTON_COLOR_REF }, stroke: null, opacity: { keyframes: [{ time: 0, value: 0.82 }, { time: 0.64, value: 0.5 }, { time: 1, value: 0 }] }, blendMode: "additive", glow: 10 }
      : { stroke: { color: BUTTON_COLOR_REF, width: Math.max(1.5, 3 - index * 0.35) }, glow: 6 }),
  })));
}

const burst = particleEffect("builtin.burst", "Explosão", "Raios sólidos partem do ponto de impacto.", { kind: "rect", size: [13, 2], cornerRadius: 1 }, { count: 18, speed: 190, gravity: [0, 0], distribution: "even" });
const spark = shapeEffect("builtin.spark", "Faísca", "Estrela dinâmica com giro curto.", [{ kind: "star", outerRadius: 48, innerRadius: 13, points: 7 }], false, 28);
const focus = shapeEffect("builtin.focus", "Foco", "Enquadramento geométrico em contração.", [{ kind: "rect", size: [92, 92], cornerRadius: 9 }, { kind: "rect", size: [58, 58], cornerRadius: 5 }]);
const halo = shapeEffect("builtin.halo", "Halo", "Camadas preenchidas de luz atmosférica.", [{ kind: "circle", radius: 48 }, { kind: "ring", radius: 37 }], true);
const shockwave = shapeEffect("builtin.shockwave", "Impacto", "Ondas de choque escalonadas.", [{ kind: "ring", radius: 58 }, { kind: "ring", radius: 43 }, { kind: "ring", radius: 27 }]);
const orbit = particleEffect("builtin.orbit", "Órbita", "Satélites em expansão radial simétrica.", { kind: "circle", radius: 5 }, { count: 8, speed: 92, gravity: [0, 0], distribution: "even" });
const petals = particleEffect("builtin.petals", "Pétalas", "Pétalas sólidas formam uma flor em movimento.", { kind: "diamond", size: [9, 17] }, { count: 10, speed: 105, gravity: [0, 20], distribution: "even" });
const diamond = shapeEffect("builtin.diamond", "Diamante", "Losangos concêntricos rotativos.", [{ kind: "diamond", size: [86, 86] }, { kind: "diamond", size: [58, 58] }, { kind: "diamond", size: [32, 32] }], false, 24);
const sonar = shapeEffect("builtin.sonar", "Sonar", "Anéis e feixe de varredura direcional.", [{ kind: "ring", radius: 52 }, { kind: "ring", radius: 34 }, { kind: "line", start: [0, 0], end: [62, 0] }], false, 38);
const vortex = particleEffect("builtin.vortex", "Vórtice", "Orbes densos criam uma espiral energética.", { kind: "circle", radius: 4 }, { count: 26, speed: 115, gravity: [18, -12] });
const cross = shapeEffect("builtin.cross", "Cruz", "Dois eixos sólidos atravessam o clique.", [{ kind: "line", start: [-58, 0], end: [58, 0] }, { kind: "line", start: [0, -58], end: [0, 58] }]);
const lightning = particleEffect("builtin.lightning", "Relâmpago", "Fragmentos elétricos rápidos e brilhantes.", { kind: "polygon", radius: 7, sides: 3 }, { count: 14, speed: 205, gravity: [0, 45] });
const heart = shapeEffect("builtin.heart", "Coração", "Pulso preenchido de aparência orgânica.", [{ kind: "circle", radius: 25 }, { kind: "circle", radius: 17 }], true, 12);
const ink = shapeEffect("builtin.ink", "Tinta", "Manchas preenchidas sobrepostas e macias.", [{ kind: "circle", radius: 43 }, { kind: "circle", radius: 27 }, { kind: "circle", radius: 16 }], true, 16);
const splash = particleEffect("builtin.splash", "Splash", "Gotas cheias em alta velocidade.", { kind: "circle", radius: 5 }, { count: 20, speed: 210, gravity: [0, 125] });
const comet = particleEffect("builtin.comet", "Cometa", "Núcleos luminosos com trajetória expansiva.", { kind: "circle", radius: 6 }, { count: 9, speed: 175, gravity: [-25, 65] });
const eclipse = shapeEffect("builtin.eclipse", "Eclipse", "Discos preenchidos em oposição orbital.", [{ kind: "circle", radius: 44 }, { kind: "circle", radius: 30 }, { kind: "ring", radius: 52 }], true, 20);
const plasma = particleEffect("builtin.plasma", "Plasma", "Orbes luminosos fluidos com variações determinísticas.", { kind: "circle", radius: 9 }, { count: 13, speed: 88, gravity: [0, -20] });
const prism = particleEffect("builtin.prism", "Prisma", "Fragmentos triangulares aditivos.", { kind: "polygon", radius: 9, sides: 3 }, { count: 18, speed: 165, gravity: [0, 70] });
const flower = particleEffect("builtin.flower", "Flor cheia", "Pétalas sólidas giram a partir do centro.", { kind: "circle", radius: 8 }, { count: 12, speed: 118, gravity: [0, 15], distribution: "even" });
const meteor = particleEffect("builtin.meteor", "Meteoro", "Lascas densas criam um impacto direcional.", { kind: "diamond", size: [8, 15] }, { count: 17, speed: 225, gravity: [35, 105] });

export const BUILT_IN_EFFECTS: readonly EffectDocument[] = Object.freeze([
  ripple, pulse, target, burst, spark, focus, halo, shockwave, orbit, petals,
  diamond, sonar, vortex, cross, confetti, lightning, bubbles, heart,
  ink, splash, nova, comet, eclipse, plasma, pixels, prism, flower, meteor,
]);

export function createBlankEffect(sequence = Date.now()): EffectDocument {
  return document(
    `custom.effect-${sequence}`,
    "Novo efeito",
    "Efeito criado no editor do RadiantCursor Studio.",
    600,
    [{
      ...base("layer-1", "Anel", 600),
      type: "shape",
      geometry: { kind: "ring", radius: 42 },
      transform: { ...DEFAULT_TRANSFORM, scale: { keyframes: [{ time: 0, value: [0.1, 0.1], easing: "easeOutCubic" }, { time: 1, value: [1, 1] }] } },
      material: material(),
    }],
  );
}

export function cloneEffect(document: EffectDocument): EffectDocument {
  return structuredClone(document);
}
