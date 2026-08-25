import type { EffectDocumentV2, EngineDiagnostic, ValidationResult } from "./schema";

export const MOTION_LIMITS = Object.freeze({ maxElements: 128, maxShapes: 96, maxDepth: 8, maxAnimations: 512, maxTracks: 512, maxKeyframes: 64, maxDurationMs: 10_000, maxDocumentBytes: 512 * 1024 });
const finite = (value: unknown): value is number => typeof value === "number" && Number.isFinite(value);
export function validateMotionDocument(input: unknown): ValidationResult {
  const diagnostics: EngineDiagnostic[] = []; const error = (code: string, path: string, message: string) => diagnostics.push({ severity: "error", code, path, message });
  if (!input || typeof input !== "object") return { valid: false, diagnostics: [{ severity: "error", code: "document", path: "$", message: "Documento inválido." }] };
  const doc = input as Partial<EffectDocumentV2>;
  if (doc.schemaVersion !== 2) error("schema", "$.schemaVersion", "O documento deve usar o schema 2.");
  if (doc.target !== undefined && doc.target !== "click" && doc.target !== "halo") error("target", "$.target", "O destino deve ser clique ou halo.");
  if (typeof doc.id !== "string" || !/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$/.test(doc.id)) error("id", "$.id", "ID inválido.");
  if (!finite(doc.durationMs) || doc.durationMs < 1 || doc.durationMs > MOTION_LIMITS.maxDurationMs) error("duration", "$.durationMs", "Duração total fora dos limites.");
  if (!Array.isArray(doc.rootIds) || !doc.elements || typeof doc.elements !== "object") error("hierarchy", "$.elements", "Hierarquia ausente.");
  else {
    const elements = doc.elements; const ids = Object.keys(elements); if (ids.length < 1 || ids.length > MOTION_LIMITS.maxElements) error("element_limit", "$.elements", "Quantidade de elementos fora dos limites.");
    if (ids.filter((id) => elements[id]?.type === "shape").length > MOTION_LIMITS.maxShapes) error("shape_limit", "$.elements", "Há formas demais.");
    const seen = new Set<string>(), active = new Set<string>(); let animationCount = 0, trackCount = 0;
    const visit = (id: string, depth: number, path: string) => {
      if (active.has(id)) return error("cycle", path, "A hierarquia contém um ciclo."); if (seen.has(id)) return error("duplicate_child", path, "Elemento referenciado mais de uma vez.");
      const element = elements[id]; if (!element) return error("missing_element", path, `Elemento ${id} não existe.`); if (element.id !== id) error("identity", `${path}.id`, "A chave e o ID devem ser iguais."); if (depth > MOTION_LIMITS.maxDepth) error("depth", path, "Profundidade máxima excedida.");
      if (!finite(element.timing.startMs) || element.timing.startMs < 0 || !finite(element.timing.durationMs) || element.timing.durationMs < 1) error("timing", `${path}.timing`, "Timing inválido.");
      if (element.transform.size.some((v) => !finite(v) || v <= 0) || element.transform.scale.some((v) => !finite(v) || Math.abs(v) > 100)) error("transform", `${path}.transform`, "Transformação inválida.");
      animationCount += element.animations.length; active.add(id); seen.add(id);
      for (const animation of element.animations) { if (animation.type === "tracks") { trackCount += animation.tracks.length; for (const track of animation.tracks) if (track.keyframes.length < 2 || track.keyframes.length > MOTION_LIMITS.maxKeyframes || track.keyframes.some((frame) => !finite(frame.time) || frame.time < 0 || frame.time > 1 || !finite(frame.value))) error("keyframes", `${path}.animations`, "Keyframes inválidos."); } }
      if (element.type === "group") element.children.forEach((child, i) => visit(child, depth + 1, `${path}.children[${i}]`)); active.delete(id);
    };
    doc.rootIds.forEach((id, i) => visit(id, 0, `$.rootIds[${i}]`)); for (const id of ids) if (!seen.has(id)) error("orphan", `$.elements.${id}`, "Elemento órfão.");
    if (animationCount > MOTION_LIMITS.maxAnimations || trackCount > MOTION_LIMITS.maxTracks) error("animation_limit", "$.elements", "Orçamento de animações excedido.");
  }
  try { if (JSON.stringify(input).length > MOTION_LIMITS.maxDocumentBytes) error("document_size", "$", "Documento grande demais."); } catch { error("serialize", "$", "Documento não serializável."); }
  return { valid: !diagnostics.some((entry) => entry.severity === "error"), diagnostics, ...(diagnostics.some((entry) => entry.severity === "error") ? {} : { document: input as EffectDocumentV2 }) };
}
export function assertMotionDocument(input: unknown): EffectDocumentV2 { const result = validateMotionDocument(input); if (!result.valid || !result.document) throw Object.assign(new Error(result.diagnostics[0]?.message ?? "Documento inválido."), { diagnostics: result.diagnostics }); return result.document; }
