import type { EasingName } from "./schema";
export function ease(name: EasingName, value: number): number {
  const x = Math.max(0, Math.min(1, value));
  if (name === "easeInQuad") return x * x;
  if (name === "easeOutQuad") return 1 - (1 - x) ** 2;
  if (name === "easeInOutQuad") return x < .5 ? 2 * x * x : 1 - (-2 * x + 2) ** 2 / 2;
  if (name === "easeInCubic") return x ** 3;
  if (name === "easeOutCubic") return 1 - (1 - x) ** 3;
  if (name === "easeInOutCubic") return x < .5 ? 4 * x ** 3 : 1 - (-2 * x + 2) ** 3 / 2;
  if (name === "easeOutBack") { const c = 1.70158; return 1 + (c + 1) * (x - 1) ** 3 + c * (x - 1) ** 2; }
  if (name === "easeOutBounce") { const n = 7.5625, d = 2.75; if (x < 1 / d) return n * x * x; if (x < 2 / d) return n * (x - 1.5 / d) ** 2 + .75; if (x < 2.5 / d) return n * (x - 2.25 / d) ** 2 + .9375; return n * (x - 2.625 / d) ** 2 + .984375; }
  if (name === "easeOutElastic") return x === 0 || x === 1 ? x : 2 ** (-10 * x) * Math.sin((x * 10 - .75) * 2 * Math.PI / 3) + 1;
  return x;
}
