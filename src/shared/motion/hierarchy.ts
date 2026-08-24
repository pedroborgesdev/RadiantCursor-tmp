import { IDENTITY, multiply, transformMatrix, type Matrix } from "./math";
import type { EffectDocumentV2, ElementDocument } from "./schema";

export function parentMap(document: EffectDocumentV2): Map<string, string | null> {
  const result = new Map<string, string | null>(); document.rootIds.forEach((id) => result.set(id, null));
  for (const element of Object.values(document.elements)) if (element.type === "group") element.children.forEach((id) => result.set(id, element.id));
  return result;
}
export function flattenElements(document: EffectDocumentV2): Array<{ element: ElementDocument; depth: number; parentId: string | null }> {
  const result: Array<{ element: ElementDocument; depth: number; parentId: string | null }> = [];
  const visit = (id: string, depth: number, parentId: string | null) => { const element = document.elements[id]; if (!element) return; result.push({ element, depth, parentId }); if (element.type === "group") element.children.forEach((child) => visit(child, depth + 1, id)); };
  document.rootIds.forEach((id) => visit(id, 0, null)); return result;
}
export function descendants(document: EffectDocumentV2, id: string): string[] {
  const result: string[] = []; const visit = (next: string) => { const element = document.elements[next]; if (!element) return; result.push(next); if (element.type === "group") element.children.forEach(visit); }; visit(id); return result;
}
export function worldMatrix(document: EffectDocumentV2, id: string): Matrix {
  const parents = parentMap(document); const chain: ElementDocument[] = []; let cursor: string | null | undefined = id;
  while (cursor) { const element = document.elements[cursor]; if (!element) break; chain.unshift(element); cursor = parents.get(cursor); }
  return chain.reduce((matrix, element) => multiply(matrix, transformMatrix(element.transform)), IDENTITY);
}
export function removeFromHierarchy(document: EffectDocumentV2, ids: ReadonlySet<string>): void {
  document.rootIds = document.rootIds.filter((id) => !ids.has(id));
  for (const element of Object.values(document.elements)) if (element.type === "group") element.children = element.children.filter((id) => !ids.has(id));
  for (const id of ids) for (const child of descendants(document, id)) delete document.elements[child];
}
export function scaleGroupTiming(document: EffectDocumentV2, groupId: string, newDurationMs: number): void {
  const group = document.elements[groupId]; if (!group || group.type !== "group" || group.timing.durationMs <= 0) return;
  const ratio = newDurationMs / group.timing.durationMs; group.timing.durationMs = newDurationMs;
  const visit = (id: string) => { const element = document.elements[id]; if (!element) return; element.timing.startMs = Math.round(element.timing.startMs * ratio); element.timing.durationMs = Math.max(1, Math.round(element.timing.durationMs * ratio)); element.animations.forEach((animation) => { animation.timing.startMs = Math.round(animation.timing.startMs * ratio); animation.timing.durationMs = Math.max(1, Math.round(animation.timing.durationMs * ratio)); }); if (element.type === "group") element.children.forEach(visit); };
  group.children.forEach(visit);
}

