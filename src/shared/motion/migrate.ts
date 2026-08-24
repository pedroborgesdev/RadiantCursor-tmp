import type { EffectDocument as LegacyEffectDocument, ShapeLayerDocument } from "../engine/schema";
import { createBlankMotionEffect, createShape } from "./builtins";
import type { EffectDocumentV2, ShapeKind } from "./schema";

const shapeKind = (layer: ShapeLayerDocument): ShapeKind => {
  const kind = layer.geometry.kind; if (kind === "rect") return "rectangle"; if (kind === "ring" || kind === "circle") return "circle"; if (kind === "diamond") return "diamond"; if (kind === "star") return "star"; if (kind === "line") return "line"; if (kind === "polygon") return layer.geometry.sides === 3 ? "triangle" : layer.geometry.sides === 6 ? "hexagon" : "polygon"; return "circle";
};
export function migrateLegacyDocument(input: unknown): EffectDocumentV2 | null {
  if (!input || typeof input !== "object" || (input as { schemaVersion?: unknown }).schemaVersion !== 1) return null;
  const legacy = input as LegacyEffectDocument; const document = createBlankMotionEffect(legacy.metadata?.name ?? "Efeito migrado");
  document.id = legacy.id; document.durationMs = legacy.durationMs; document.metadata = { ...legacy.metadata, tags: [...(legacy.metadata.tags ?? []).filter((tag) => tag !== "built-in"), "migrated-v1"] }; document.rootIds = []; document.elements = {};
  for (const layer of legacy.layers ?? []) {
    if (layer.type !== "shape") continue; const position = Array.isArray(layer.transform.position) ? [Number(layer.transform.position[0]), Number(layer.transform.position[1])] as const : [0, 0] as const; const shape = createShape(shapeKind(layer), position, Boolean(layer.material.fill), layer.timing.durationMs);
    shape.id = layer.id; shape.name = layer.name; shape.visible = layer.enabled; shape.timing = { ...layer.timing }; shape.transform.rotationDeg = typeof layer.transform.rotationDeg === "number" ? layer.transform.rotationDeg : 0; shape.transform.scale = Array.isArray(layer.transform.scale) ? [Number(layer.transform.scale[0]), Number(layer.transform.scale[1])] : [1, 1];
    if ("radius" in layer.geometry) shape.transform.size = [layer.geometry.radius * 2, layer.geometry.radius * 2]; else if ("size" in layer.geometry) shape.transform.size = layer.geometry.size;
    shape.appearance.fill.enabled = Boolean(layer.material.fill); shape.appearance.stroke.enabled = Boolean(layer.material.stroke); if (typeof layer.material.fill?.color === "string") shape.appearance.fill.color = layer.material.fill.color; if (typeof layer.material.stroke?.color === "string") shape.appearance.stroke.color = layer.material.stroke.color; if (typeof layer.material.stroke?.width === "number") shape.appearance.stroke.width = layer.material.stroke.width;
    document.rootIds.push(shape.id); document.elements[shape.id] = shape;
  }
  if (!document.rootIds.length) { const fallback = createBlankMotionEffect(document.metadata.name); fallback.id = document.id; fallback.metadata = document.metadata; return fallback; }
  return document;
}
