import type { EasingName } from "./schema";

export function evaluateEasing(name: EasingName, input: number): number {
  const x = Math.min(1, Math.max(0, input));
  switch (name) {
    case "linear": return x;
    case "easeInQuad": return x * x;
    case "easeOutQuad": return 1 - (1 - x) * (1 - x);
    case "easeInOutQuad": return x < 0.5 ? 2 * x * x : 1 - ((-2 * x + 2) ** 2) / 2;
    case "easeInCubic": return x ** 3;
    case "easeOutCubic": return 1 - (1 - x) ** 3;
    case "easeInOutCubic": return x < 0.5 ? 4 * x ** 3 : 1 - ((-2 * x + 2) ** 3) / 2;
    case "easeOutBack": {
      const c1 = 1.70158;
      const c3 = c1 + 1;
      return 1 + c3 * (x - 1) ** 3 + c1 * (x - 1) ** 2;
    }
  }
}

export function sampleAnimatable<T>(
  value: T | { keyframes: Array<{ time: number; value: T; easing?: EasingName }> },
  progress: number,
  interpolate: (from: T, to: T, amount: number) => T,
): T {
  if (!(typeof value === "object" && value !== null && "keyframes" in value)) {
    return value;
  }
  const frames = value.keyframes;
  if (frames.length === 0) throw new Error("Uma track precisa ter keyframes.");
  if (progress <= frames[0]!.time) return frames[0]!.value;
  for (let index = 1; index < frames.length; index += 1) {
    const next = frames[index]!;
    const previous = frames[index - 1]!;
    if (progress <= next.time) {
      const span = Math.max(0.000001, next.time - previous.time);
      const local = evaluateEasing(previous.easing ?? "linear", (progress - previous.time) / span);
      return interpolate(previous.value, next.value, local);
    }
  }
  return frames[frames.length - 1]!.value;
}
