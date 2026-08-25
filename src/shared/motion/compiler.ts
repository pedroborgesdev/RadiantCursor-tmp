import { applyMatrix, invert } from "./math";
import { flattenElements, worldMatrix } from "./hierarchy";
import type { AnimationDocument, AnimationTrack, EffectDocumentV2, ElementDocument, RuntimeChannel, RuntimeDefinition } from "./schema";

function frames(from: number, to: number, easing: AnimationDocument["easing"]): AnimationTrack["keyframes"] { return [{ time: 0, value: from, easing }, { time: 1, value: to, easing }]; }
function recipe(element: ElementDocument, animation: Extract<AnimationDocument, { type: "preset" }>, document: EffectDocumentV2): AnimationTrack[] {
  const amount = animation.params.amount ?? .25, distance = animation.params.distance ?? 80, angle = (animation.params.angleDeg ?? 0) * Math.PI / 180;
  const add = (property: AnimationTrack["property"], from: number, to: number): AnimationTrack => ({ property, composition: "add", keyframes: frames(from, to, animation.easing) });
  const mul = (property: AnimationTrack["property"], from: number, to: number): AnimationTrack => ({ property, composition: "multiply", keyframes: frames(from, to, animation.easing) });
  switch (animation.preset) {
    case "fadeIn": return [mul("opacity", 0, 1)]; case "fadeOut": return [mul("opacity", 1, 0)];
    case "scaleIn": return [mul("scale.x", 0, 1), mul("scale.y", 0, 1)]; case "scaleOut": case "shrinkOut": return [mul("scale.x", 1, 0), mul("scale.y", 1, 0)];
    case "popIn": return [mul("scale.x", .1, 1), mul("scale.y", .1, 1)];
    case "slideIn": return [add("position.x", -distance, 0)]; case "slideOut": return [add("position.x", 0, distance)];
    case "dropIn": return [add("position.y", -distance, 0)]; case "dropOut": case "fall": return [add("position.y", 0, distance)]; case "rise": return [add("position.y", 0, -distance)];
    case "rotateIn": return [add("rotation", -90, 0)]; case "rotateOut": return [add("rotation", 0, 90)]; case "rotate": case "spin": return [add("rotation", 0, 360 * (animation.params.turns ?? 1))];
    case "move": return [add("position.x", 0, animation.params.x ?? Math.cos(angle) * distance), add("position.y", 0, animation.params.y ?? Math.sin(angle) * distance)];
    case "moveToward": return [add("position.x", 0, -element.transform.position[0]), add("position.y", 0, -element.transform.position[1])];
    case "moveAway": {
      const center = applyMatrix(worldMatrix(document, element.id), [element.transform.size[0] * element.transform.anchor[0], element.transform.size[1] * element.transform.anchor[1]]);
      const length = Math.hypot(center[0], center[1]) || 1; const worldDelta: readonly [number, number] = [center[0] / length * distance, center[1] / length * distance];
      const parentWorld = (() => { const flat = flattenElements(document).find((entry) => entry.element.id === element.id); return flat?.parentId ? worldMatrix(document, flat.parentId) : [1, 0, 0, 1, 0, 0] as const; })();
      const local0 = applyMatrix(invert(parentWorld), [0, 0]); const local1 = applyMatrix(invert(parentWorld), worldDelta);
      return [add("position.x", 0, local1[0] - local0[0]), add("position.y", 0, local1[1] - local0[1])];
    }
    case "shake": return [{ property: "position.x", composition: "add", keyframes: [0, .2, .4, .6, .8, 1].map((time, i) => ({ time, value: i === 5 ? 0 : (i % 2 ? distance * .15 : -distance * .15), easing: animation.easing })) }];
    case "bounce": return [{ property: "position.y", composition: "add", keyframes: [{ time: 0, value: 0 }, { time: .45, value: -distance }, { time: 1, value: 0, easing: "easeOutBounce" }] }];
    case "orbit": return [{ property: "position.x", composition: "add", keyframes: [0, .25, .5, .75, 1].map((time) => ({ time, value: Math.cos(time * Math.PI * 2) * distance - distance, easing: animation.easing })) }, { property: "position.y", composition: "add", keyframes: [0, .25, .5, .75, 1].map((time) => ({ time, value: Math.sin(time * Math.PI * 2) * distance, easing: animation.easing })) }];
    case "stretch": return [mul("scale.x", 1, 1 + amount), mul("scale.y", 1, 1 - amount)]; case "squash": return [mul("scale.x", 1, 1 - amount), mul("scale.y", 1, 1 + amount)];
    case "pulse": return [{ property: "scale.x", composition: "multiply", keyframes: [{ time: 0, value: 1 }, { time: .5, value: 1 + amount }, { time: 1, value: 1 }] }, { property: "scale.y", composition: "multiply", keyframes: [{ time: 0, value: 1 }, { time: .5, value: 1 + amount }, { time: 1, value: 1 }] }];
    case "scale": return [mul("scale.x", 1, amount), mul("scale.y", 1, amount)];
  }
}

export function compileEffect(document: EffectDocumentV2): RuntimeDefinition {
  const flat = flattenElements(document); const index = new Map(flat.map((entry, i) => [entry.element.id, i]));
  const nodes = flat.map(({ element, parentId }, nodeIndex) => {
    const channels: RuntimeChannel[] = [];
    for (const animation of element.animations) if (animation.enabled) {
      const tracks = animation.type === "tracks" ? animation.tracks : recipe(element, animation, document);
      for (const track of tracks) channels.push({ property: track.property, composition: track.composition, keyframes: track.keyframes.map((frame) => ({ time: animation.timing.startMs + frame.time * animation.timing.durationMs, value: frame.value, easing: frame.easing ?? animation.easing })) });
    }
    let subtreeEnd = nodeIndex + 1; for (let i = nodeIndex + 1; i < flat.length && flat[i]!.depth > flat[nodeIndex]!.depth; i++) subtreeEnd = i + 1;
    return { id: element.id, name: element.name, kind: element.type, parentIndex: parentId ? index.get(parentId) ?? -1 : -1, subtreeEnd, visible: element.visible, startMs: element.timing.startMs, durationMs: element.timing.durationMs, transform: element.transform, opacity: element.opacity, channels, ...(element.type === "shape" ? { shape: element.shape, appearance: element.appearance } : {}) };
  });
  let maxRadius = 1; for (const { element } of flat) maxRadius = Math.max(maxRadius, Math.hypot(element.transform.position[0], element.transform.position[1]) + Math.hypot(element.transform.size[0], element.transform.size[1]) * Math.max(...element.transform.scale));
  return { runtimeVersion: 1, compilerVersion: 1, effectId: document.id, target: document.target ?? "click", durationMs: document.durationMs, maxRadius: Math.ceil(maxRadius), nodes };
}
