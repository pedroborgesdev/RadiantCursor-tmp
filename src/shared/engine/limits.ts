export const ENGINE_LIMITS = Object.freeze({
  maxJsonBytes: 256 * 1024,
  maxLayers: 32,
  maxTracksPerLayer: 24,
  maxKeyframesPerTrack: 64,
  maxDurationMs: 10_000,
  maxParticlesPerLayer: 256,
  maxGlobalParticles: 4_096,
  maxActiveInstances: 24,
  maxAssetsPerEffect: 32,
  maxPolygonSides: 64,
  maxTextureDimension: 4_096,
  maxTextureBytesPerEffect: 64 * 1024 * 1024,
  maxExtractedBundleBytes: 128 * 1024 * 1024,
  maxNameLength: 80,
  maxDescriptionLength: 500,
  maxTags: 16,
  maxTagLength: 32,
});

export const ENGINE_VERSION = 1;
export const SUPPORTED_SCHEMA_VERSIONS = [1, 2] as const;

export const ENGINE_CAPABILITIES = Object.freeze([
  "shape.v1",
  "particles.v1",
  "image.v1",
  "blend.normal.v1",
  "blend.additive.v1",
  "motion.shapes.v2",
  "motion.hierarchy.v1",
  "motion.tracks.v1",
] as const);

export type EngineCapability = (typeof ENGINE_CAPABILITIES)[number];
