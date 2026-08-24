import type { TransformDocument, Vec2 } from "./schema";

export type Matrix = readonly [number, number, number, number, number, number];
export const IDENTITY: Matrix = [1, 0, 0, 1, 0, 0];
export function multiply(a: Matrix, b: Matrix): Matrix {
  return [a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3], a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]];
}
export function transformMatrix(value: TransformDocument): Matrix {
  const angle = value.rotationDeg * Math.PI / 180;
  const skew = Math.tan(value.skewXDeg * Math.PI / 180);
  const [sx, sy] = value.scale; const [ax, ay] = [value.anchor[0] * value.size[0], value.anchor[1] * value.size[1]];
  const translate: Matrix = [1, 0, 0, 1, value.position[0], value.position[1]];
  const rotate: Matrix = [Math.cos(angle), Math.sin(angle), -Math.sin(angle), Math.cos(angle), 0, 0];
  const skewScale: Matrix = [sx, 0, skew * sy, sy, 0, 0];
  return multiply(multiply(multiply(translate, rotate), skewScale), [1, 0, 0, 1, -ax, -ay]);
}
export function applyMatrix(matrix: Matrix, point: Vec2): Vec2 { return [matrix[0] * point[0] + matrix[2] * point[1] + matrix[4], matrix[1] * point[0] + matrix[3] * point[1] + matrix[5]]; }
export function invert(m: Matrix): Matrix {
  const det = m[0] * m[3] - m[1] * m[2]; if (Math.abs(det) < 1e-8) return IDENTITY;
  return [m[3] / det, -m[1] / det, -m[2] / det, m[0] / det, (m[2] * m[5] - m[3] * m[4]) / det, (m[1] * m[4] - m[0] * m[5]) / det];
}

